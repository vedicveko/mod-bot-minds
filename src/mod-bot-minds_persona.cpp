#include "mod-bot-minds_persona.h"
#include "mod-bot-minds-utilities.h"
#include "Log.h"
#include "DatabaseEnv.h"
#include "QueryResult.h"
#include "Field.h"
#include "Player.h"
#include "SharedDefines.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <fmt/core.h>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Config globals (defined in _config.cpp).
extern bool g_DebugEnabled;
extern bool g_Enable;

// --------------------------------------------
// In-memory cache and dirty-set.
// --------------------------------------------
static std::unordered_map<uint64_t, Persona> g_Personas;
static std::unordered_set<uint64_t>          g_PersonaDirty;
static std::mutex                            g_PersonaMutex;

namespace
{
    struct MoodState
    {
        float       score = 0.0f;
        time_t      updatedAt = 0;
        std::string cause;
    };

    std::unordered_map<uint64_t, MoodState> g_Moods;
    std::mutex                               g_MoodMutex;

    struct TemperamentAxis
    {
        char const* low;
        char const* high;
    };

    struct MoodEffect
    {
        float       delta;
        char const* cause;
    };

    constexpr time_t MOOD_HALF_LIFE_SECONDS = 8 * 60;

    std::array<std::string, 8> const LEGACY_TRAITS = {
        "blunt, loyal, impatient",
        "chatty, curious, upbeat",
        "cautious, observant, wry",
        "hot-headed, competitive, proud",
        "calm, helpful, easygoing",
        "greedy about loot and always angling for a deal",
        "friendly and a bit clueless",
        "serious and focused on making progress"
    };

    std::array<std::string, 8> const LEGACY_SPEECH_STYLES = {
        "keep it short and blunt",
        "chat a lot and joke around",
        "are dry and sarcastic",
        "are cheerful and a little over-eager",
        "are calm and matter-of-fact",
        "moan about everything, cheerfully",
        "are terse, almost monosyllabic",
        "over-explain things"
    };

    // SplitMix64 supplies independent-looking values for each profile dimension
    // without introducing mutable random state. A bot's GUID is its stable seed.
    uint64_t Mix(uint64_t value)
    {
        value += 0x9e3779b97f4a7c15ULL;
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31);
    }

    uint32_t ProfileValue(uint64_t guid, uint32_t dimension, uint32_t range)
    {
        return static_cast<uint32_t>(Mix(guid + 0x9e3779b97f4a7c15ULL * dimension) % range);
    }

    template <std::size_t N>
    bool Contains(std::array<std::string, N> const& values, std::string const& value)
    {
        return std::find(values.begin(), values.end(), value) != values.end();
    }

    std::string Join(std::vector<std::string> const& values)
    {
        std::ostringstream out;
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            if (i)
                out << (i + 1 == values.size() ? " and " : ", ");
            out << values[i];
        }
        return out.str();
    }

    std::string ArchetypeForClass(uint8 cls)
    {
        switch (cls)
        {
            case CLASS_WARRIOR:      return "Warrior";
            case CLASS_PALADIN:      return "Crusader";
            case CLASS_HUNTER:       return "Hunter";
            case CLASS_ROGUE:        return "Rogue";
            case CLASS_PRIEST:       return "Priest";
            case CLASS_DEATH_KNIGHT: return "Death Knight";
            case CLASS_SHAMAN:       return "Shaman";
            case CLASS_MAGE:         return "Mage";
            case CLASS_WARLOCK:      return "Warlock";
            case CLASS_DRUID:        return "Druid";
            default:                 return "Adventurer";
        }
    }

    // Build the stored shell of a persona. Temperament and voice are generated
    // from the GUID at prompt time, so improving the generator upgrades existing
    // rows without a destructive database migration.
    Persona GenerateTemplatePersona(uint64_t guid, std::string const& name, uint8 cls)
    {
        Persona p;
        p.guid      = guid;
        p.name      = name;
        p.archetype = ArchetypeForClass(cls);
        return p;
    }

    MoodEffect EffectFor(PersonaMoodEvent event)
    {
        switch (event)
        {
            case PersonaMoodEvent::CreatureKill:  return { 0.5f, "a good run of fights" };
            case PersonaMoodEvent::PvPWin:        return { 20.0f, "winning a fight" };
            case PersonaMoodEvent::RareLoot:      return { 16.0f, "finding a nice upgrade" };
            case PersonaMoodEvent::EpicLoot:      return { 30.0f, "finding great gear" };
            case PersonaMoodEvent::Death:         return { -35.0f, "dying" };
            case PersonaMoodEvent::QuestComplete: return { 18.0f, "finishing a quest" };
            case PersonaMoodEvent::LearnedSpell:  return { 10.0f, "learning something new" };
            case PersonaMoodEvent::DuelWin:       return { 22.0f, "winning a duel" };
            case PersonaMoodEvent::DuelLoss:      return { -15.0f, "losing a duel" };
            case PersonaMoodEvent::LevelUp:       return { 35.0f, "gaining a level" };
            case PersonaMoodEvent::Achievement:   return { 25.0f, "earning an achievement" };
            case PersonaMoodEvent::ReceivedHelp:  return { 12.0f, "someone helping you out" };
        }

        return { 0.0f, "" };
    }

    void DecayMood(MoodState& mood, time_t now)
    {
        if (mood.updatedAt == 0 || now <= mood.updatedAt)
        {
            mood.updatedAt = now;
            return;
        }

        time_t const elapsed = now - mood.updatedAt;
        mood.score *= std::pow(0.5f, static_cast<float>(elapsed) / static_cast<float>(MOOD_HALF_LIFE_SECONDS));

        mood.updatedAt = now;
    }

    // Insert a persona row immediately (used when auto-generating on first access).
    void InsertPersonaRow(Persona const& p)
    {
        std::string escName      = p.name;
        std::string escArchetype = p.archetype;
        std::string escTraits    = p.traits;
        std::string escSpeech    = p.speechStyle;
        std::string escBackstory = p.backstory;
        CharacterDatabase.EscapeString(escName);
        CharacterDatabase.EscapeString(escArchetype);
        CharacterDatabase.EscapeString(escTraits);
        CharacterDatabase.EscapeString(escSpeech);
        CharacterDatabase.EscapeString(escBackstory);

        CharacterDatabase.Execute(SafeFormat(
            "REPLACE INTO mod_bot_minds_persona "
            "(bot_guid, name, archetype, traits, speech_style, backstory) "
            "VALUES ({}, '{}', '{}', '{}', '{}', '{}')",
            p.guid, escName, escArchetype, escTraits, escSpeech, escBackstory));
    }
}

Persona const& GetPersona(Player* bot)
{
    static Persona emptyPersona;

    if (!bot)
        return emptyPersona;

    uint64_t guid = bot->GetGUID().GetRawValue();

    std::lock_guard<std::mutex> lock(g_PersonaMutex);

    auto it = g_Personas.find(guid);
    if (it != g_Personas.end())
        return it->second;

    // No persona yet: generate a deterministic template from class/name,
    // persist it, and cache it. An LLM-driven persona path can be added later.
    Persona generated = GenerateTemplatePersona(guid, bot->GetName(), bot->getClass());

    InsertPersonaRow(generated);

    auto res = g_Personas.emplace(guid, std::move(generated));

    if (g_DebugEnabled)
    {
        LOG_INFO("server.loading",
                 "[BotMinds] Generated template persona '{}' (archetype '{}') for bot {}",
                 res.first->second.name, res.first->second.archetype, bot->GetName());
    }

    return res.first->second;
}

PersonaProfile GeneratePersonaProfile(uint64_t guid)
{
    static std::array<TemperamentAxis, 7> const axes = {{
        { "reserved with strangers", "quick to join a conversation" },
        { "easily impatient", "patient" },
        { "skeptical", "optimistic" },
        { "careful about risks", "bold" },
        { "laid-back", "competitive" },
        { "protective of your own time and loot", "generous with help" },
        { "fairly serious", "playful" }
    }};
    static std::array<char const*, 12> const interests = {{
        "finishing quest chains", "dungeon runs", "battlegrounds", "working on professions",
        "exploring out-of-the-way places", "finding useful gear", "collecting mounts and pets",
        "making gold", "helping groupmates", "trying difficult fights", "earning achievements",
        "meeting other players"
    }};
    static std::array<char const*, 3> const lengths = {{
        "usually reply in a clipped phrase", "usually use one compact sentence",
        "sometimes use two short sentences when something needs explaining"
    }};
    static std::array<char const*, 3> const casing = {{
        "use normal capitalization", "usually type in lowercase", "mix normal case with quick lowercase replies"
    }};
    static std::array<char const*, 3> const wordChoice = {{
        "rarely use game slang", "occasionally use familiar game shorthand", "prefer plain everyday words"
    }};

    struct RankedAxis
    {
        uint32_t index;
        uint32_t distance;
        bool high;
    };

    std::array<RankedAxis, 7> ranked;
    for (uint32_t i = 0; i < axes.size(); ++i)
    {
        uint32_t const value = ProfileValue(guid, i + 1, 101);
        ranked[i] = { i, static_cast<uint32_t>(std::abs(static_cast<int32_t>(value) - 50)), value >= 50 };
    }
    std::sort(ranked.begin(), ranked.end(), [](RankedAxis const& left, RankedAxis const& right)
    {
        if (left.distance != right.distance)
            return left.distance > right.distance;
        return left.index < right.index;
    });

    std::vector<std::string> temperament;
    for (uint32_t i = 0; i < 3; ++i)
    {
        RankedAxis const& choice = ranked[i];
        temperament.emplace_back(choice.high ? axes[choice.index].high : axes[choice.index].low);
    }

    uint32_t const firstInterest = ProfileValue(guid, 20, interests.size());
    uint32_t secondInterest = ProfileValue(guid, 21, interests.size() - 1);
    if (secondInterest >= firstInterest)
        ++secondInterest;

    PersonaProfile profile;
    profile.temperament = Join(temperament);
    profile.interests = fmt::format("{} and {}", interests[firstInterest], interests[secondInterest]);
    profile.voice = fmt::format("{}, {}, and {}",
                                lengths[ProfileValue(guid, 30, lengths.size())],
                                casing[ProfileValue(guid, 31, casing.size())],
                                wordChoice[ProfileValue(guid, 32, wordChoice.size())]);
    return profile;
}

bool IsLegacyGeneratedTraits(std::string const& traits)
{
    return Contains(LEGACY_TRAITS, traits);
}

bool IsLegacyGeneratedSpeechStyle(std::string const& speechStyle)
{
    return Contains(LEGACY_SPEECH_STYLES, speechStyle);
}

void ApplyPersonaMood(Player* bot, PersonaMoodEvent event)
{
    if (!g_Enable || !bot)
        return;

    MoodEffect const effect = EffectFor(event);
    if (effect.delta == 0.0f)
        return;

    time_t const now = time(nullptr);
    std::lock_guard<std::mutex> lock(g_MoodMutex);
    MoodState& mood = g_Moods[bot->GetGUID().GetRawValue()];
    DecayMood(mood, now);

    // A major event should be felt even if it follows a streak in the other
    // direction. Small events accumulate into the sense of a good or bad run.
    float next = mood.score + effect.delta;
    if (std::abs(effect.delta) >= 25.0f)
        next = effect.delta > 0.0f ? std::max(next, effect.delta) : std::min(next, effect.delta);

    mood.score = std::clamp(next, -100.0f, 100.0f);
    if (mood.cause.empty() || (mood.score >= 0.0f) == (effect.delta >= 0.0f))
        mood.cause = effect.cause;
}

std::string DescribePersonaMood(Player* bot)
{
    if (!bot)
        return "";

    time_t const now = time(nullptr);
    std::lock_guard<std::mutex> lock(g_MoodMutex);
    auto iterator = g_Moods.find(bot->GetGUID().GetRawValue());
    if (iterator == g_Moods.end())
        return "";

    DecayMood(iterator->second, now);
    MoodState const& mood = iterator->second;
    if (std::abs(mood.score) < 5.0f)
        return "";

    char const* feeling = "a little upbeat";
    if (mood.score >= 45.0f)
        feeling = "in a great mood";
    else if (mood.score >= 25.0f)
        feeling = "in a good mood";
    else if (mood.score <= -45.0f)
        feeling = "in a foul mood";
    else if (mood.score <= -25.0f)
        feeling = "frustrated";
    else if (mood.score < 0.0f)
        feeling = "a little annoyed";

    return fmt::format("You are {} after {}. Let that color your tone subtly; mention why only if relevant.",
                       feeling, mood.cause);
}

void LoadPersonasFromDB()
{
    std::lock_guard<std::mutex> lock(g_PersonaMutex);
    g_Personas.clear();
    g_PersonaDirty.clear();

    QueryResult result = CharacterDatabase.Query(
        "SELECT bot_guid, name, archetype, traits, speech_style, backstory FROM mod_bot_minds_persona");

    if (!result)
    {
        LOG_INFO("server.loading", "[BotMinds] No existing persona data found in database");
        return;
    }

    uint32_t count = 0;
    do
    {
        Field* fields = result->Fetch();
        Persona p;
        p.guid        = fields[0].Get<uint64_t>();
        p.name        = fields[1].Get<std::string>();
        p.archetype   = fields[2].Get<std::string>();
        p.traits      = fields[3].Get<std::string>();
        p.speechStyle = fields[4].Get<std::string>();
        p.backstory   = fields[5].Get<std::string>();

        g_Personas[p.guid] = std::move(p);
        ++count;
    } while (result->NextRow());

    LOG_INFO("server.loading", "[BotMinds] Loaded {} persona records from database", count);
}

void FlushPersonasToDB()
{
    std::lock_guard<std::mutex> lock(g_PersonaMutex);

    if (g_PersonaDirty.empty())
        return;

    for (uint64_t guid : g_PersonaDirty)
    {
        auto it = g_Personas.find(guid);
        if (it == g_Personas.end())
            continue;

        InsertPersonaRow(it->second);
    }

    if (g_DebugEnabled)
    {
        LOG_INFO("server.loading", "[BotMinds] Flushed {} dirty persona records to database",
                 static_cast<uint32_t>(g_PersonaDirty.size()));
    }

    g_PersonaDirty.clear();
}
