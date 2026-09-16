#include "mod-bot-minds_transcript.h"
#include "mod-bot-minds_config.h"

#include "Channel.h"
#include "DBCStores.h"
#include "Group.h"
#include "Player.h"
#include "PlayerbotAI.h"

#include <ctime>
#include <deque>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace
{
    bool ChannelNameContainsArea(std::string const& channelName, AreaTableEntry const* area)
    {
        if (!area)
            return false;

        for (uint8_t locale = 0; locale < 16; ++locale)
        {
            char const* areaName = area->area_name[locale];
            if (areaName && *areaName && channelName.find(areaName) != std::string::npos)
                return true;
        }
        return false;
    }

    struct Line
    {
        std::string name;
        std::string text;
        uint32_t    atSec = 0;
    };

    struct Conversation
    {
        std::deque<Line> lines;
        uint32_t         lastActivitySec = 0;
    };

    // One floor per (scope, person): who that person is talking to right now.
    struct FloorKey
    {
        ScopeKey scope;
        uint64_t humanGuid = 0;

        bool operator==(const FloorKey& other) const
        {
            return scope == other.scope && humanGuid == other.humanGuid;
        }
    };

    struct FloorKeyHash
    {
        size_t operator()(const FloorKey& k) const
        {
            return ScopeKeyHash()(k.scope) ^ (static_cast<size_t>(k.humanGuid) << 17);
        }
    };

    std::unordered_map<ScopeKey, Conversation, ScopeKeyHash> g_Conversations;
    std::unordered_map<FloorKey, FloorHolder, FloorKeyHash>  g_Floors;
    std::mutex                                              g_TranscriptMutex;

    uint32_t NowSec()
    {
        return static_cast<uint32_t>(time(nullptr));
    }
}

bool IsInChannelInstance(Player* player, Channel const* channel)
{
    if (!player || !channel || !player->IsInChannel(channel))
        return false;

    switch (channel->GetChannelId())
    {
        case ChatChannelId::GENERAL:
        case ChatChannelId::LOCAL_DEFENSE:
            return ChannelNameContainsArea(channel->GetName(), GetAreaEntryByAreaID(player->GetZoneId()));
        case ChatChannelId::TRADE:
        case ChatChannelId::GUILD_RECRUITMENT:
            return ChannelNameContainsArea(channel->GetName(), GetAreaEntryByAreaID(3459));
        case ChatChannelId::LOOKING_FOR_GROUP:
        case ChatChannelId::WORLD_DEFENSE:
            return true;
        default:
            return false;
    }
}

ScopeKey MakeScope(ChatScope scope, Player* actor, Player* counterpart,
                   uint32_t channelId, const std::string& channelName)
{
    ScopeKey key;
    key.scope = scope;

    switch (scope)
    {
        case ChatScope::Say:
        {
            Player* audience = counterpart ? counterpart : actor;
            key.id = audience ? audience->GetGUID().GetRawValue() : 0;
            break;
        }
        case ChatScope::Party:
            if (actor && actor->GetGroup())
                key.id = actor->GetGroup()->GetGUID().GetRawValue();
            break;
        case ChatScope::Guild:
            key.id = actor ? actor->GetGuildId() : 0;
            break;
        case ChatScope::Channel:
        {
            // DBC channel ids identify a kind of channel, not an instance. Every
            // localized General channel is id 1, and the Alliance and Horde have
            // separate managers. Hash all three pieces so transcripts, pacing and
            // ambient scheduling describe the channel the player can really hear.
            uint64_t hash = 14695981039346656037ULL;
            auto mix = [&](unsigned char byte)
            {
                hash ^= byte;
                hash *= 1099511628211ULL;
            };

            uint32_t const team = actor ? static_cast<uint32_t>(actor->GetTeamId()) : 0;
            for (uint32_t value : {team, channelId})
            {
                for (uint8_t shift = 0; shift < 32; shift += 8)
                    mix(static_cast<unsigned char>((value >> shift) & 0xff));
            }
            for (unsigned char character : channelName)
                mix(character);

            key.id = hash;
            break;
        }
        case ChatScope::Whisper:
            key.id = counterpart ? counterpart->GetGUID().GetRawValue() : 0;
            break;
    }

    return key;
}

const char* ScopeName(ChatScope scope)
{
    switch (scope)
    {
        case ChatScope::Say:     return "Say";
        case ChatScope::Party:   return "Party";
        case ChatScope::Guild:   return "Guild";
        case ChatScope::Channel: return "Channel";
        case ChatScope::Whisper: return "Whisper";
    }
    return "Unknown";
}

void RecordChatLine(const ScopeKey& key, uint64_t /*speakerGuid*/, const std::string& speakerName,
                    const std::string& text)
{
    if (text.empty())
        return;

    uint32_t now = NowSec();

    std::lock_guard<std::mutex> lock(g_TranscriptMutex);

    Conversation& conv = g_Conversations[key];
    conv.lines.push_back({ speakerName, text, now });
    conv.lastActivitySec = now;

    size_t cap = g_TranscriptLines > 0 ? g_TranscriptLines : 8;
    while (conv.lines.size() > cap)
        conv.lines.pop_front();
}

std::string RenderTranscript(const ScopeKey& key, const std::string& excludeTrailing)
{
    std::lock_guard<std::mutex> lock(g_TranscriptMutex);

    auto it = g_Conversations.find(key);
    if (it == g_Conversations.end() || it->second.lines.empty())
        return "";

    const std::deque<Line>& lines = it->second.lines;

    // Build newest-first so the character cap drops the oldest lines, then flip.
    std::deque<std::string> rendered;
    size_t used = 0;
    size_t cap  = g_TranscriptMaxChars > 0 ? g_TranscriptMaxChars : 600;

    auto first = lines.rbegin();
    if (!excludeTrailing.empty() && first != lines.rend() && first->text == excludeTrailing)
        ++first;

    for (auto line = first; line != lines.rend(); ++line)
    {
        std::string entry = line->name + ": " + line->text + "\n";
        if (used + entry.size() > cap && !rendered.empty())
            break;
        used += entry.size();
        rendered.push_front(std::move(entry));
    }

    std::ostringstream out;
    for (const std::string& entry : rendered)
        out << entry;

    return out.str();
}

void SetConversationFloor(const ScopeKey& key, uint64_t humanGuid, uint64_t botGuid)
{
    if (humanGuid == 0 || botGuid == 0)
        return;

    std::lock_guard<std::mutex> lock(g_TranscriptMutex);

    FloorHolder& floor = g_Floors[FloorKey{ key, humanGuid }];
    floor.guid  = botGuid;
    floor.atSec = NowSec();
}

FloorHolder GetConversationFloor(const ScopeKey& key, uint64_t humanGuid)
{
    std::lock_guard<std::mutex> lock(g_TranscriptMutex);

    auto it = g_Floors.find(FloorKey{ key, humanGuid });
    if (it == g_Floors.end())
        return FloorHolder();

    return it->second;
}

void PruneTranscripts()
{
    uint32_t now = NowSec();
    uint32_t ttl = g_TranscriptTtlSec > 0 ? g_TranscriptTtlSec : 900;

    std::lock_guard<std::mutex> lock(g_TranscriptMutex);

    for (auto it = g_Conversations.begin(); it != g_Conversations.end(); )
    {
        if (now - it->second.lastActivitySec > ttl)
            it = g_Conversations.erase(it);
        else
            ++it;
    }

    for (auto it = g_Floors.begin(); it != g_Floors.end(); )
    {
        if (now - it->second.atSec > ttl)
            it = g_Floors.erase(it);
        else
            ++it;
    }
}
