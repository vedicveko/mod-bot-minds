#ifndef MOD_BOT_MINDS_PERSONA_H
#define MOD_BOT_MINDS_PERSONA_H

#include <string>
#include <cstdint>

class Player;

// --------------------------------------------
// Persona: a persistent identity for a bot.
// Each bot has one persona row keyed by its GUID.
// --------------------------------------------
struct Persona
{
    uint64_t    guid = 0;
    std::string name;
    std::string archetype;
    std::string traits;
    std::string speechStyle;
    std::string backstory;
};

// The generated part of a persona is deliberately not stored in the database.
// It is derived from the bot GUID, so old and new bots get the same richer
// profile on every restart while hand-written database fields remain intact.
struct PersonaProfile
{
    std::string temperament;
    std::string interests;
    std::string voice;
};

enum class PersonaMoodEvent : uint8_t
{
    CreatureKill,
    PvPWin,
    RareLoot,
    EpicLoot,
    Death,
    QuestComplete,
    LearnedSpell,
    DuelWin,
    DuelLoss,
    LevelUp,
    Achievement,
    ReceivedHelp
};

// Returns the persona for the given bot. If none exists in cache or DB,
// a template persona is generated deterministically from the bot's
// class/name, inserted into the DB, and cached.
Persona const& GetPersona(Player* bot);

// Stable generated temperament, interests and chat habits for a bot. This is
// cheap enough to build when a prompt or an administrator command needs it.
PersonaProfile GeneratePersonaProfile(uint64_t guid);

// The original generator used one of eight stock strings for each field. They
// remain in existing databases, but the richer generated profile supersedes
// them. Any other value is treated as a hand-written override and kept.
bool IsLegacyGeneratedTraits(std::string const& traits);
bool IsLegacyGeneratedSpeechStyle(std::string const& speechStyle);

// Mood is short-lived runtime state, not part of the permanent identity. Game
// events update it and it gradually fades back to neutral.
void ApplyPersonaMood(Player* bot, PersonaMoodEvent event);
std::string DescribePersonaMood(Player* bot);

// Load all personas from the database into the in-memory cache.
void LoadPersonasFromDB();

// Upsert all dirty (created/modified) personas back to the database.
void FlushPersonasToDB();

#endif // MOD_BOT_MINDS_PERSONA_H
