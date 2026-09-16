#ifndef MOD_BOT_MINDS_TRANSCRIPT_H
#define MOD_BOT_MINDS_TRANSCRIPT_H

#include <cstddef>
#include <cstdint>
#include <string>

class Player;
class Channel;

// --------------------------------------------
// Where a line was spoken. One transcript is kept per scope instance, so party
// talk, guild talk and local say never bleed into each other.
// --------------------------------------------
enum class ChatScope : uint8_t
{
    Say = 0,
    Party,
    Guild,
    Channel,
    Whisper
};

struct ScopeKey
{
    ChatScope scope = ChatScope::Say;
    uint64_t  id    = 0;   // local audience for Say, group/guild/channel id, bot's guid for Whisper

    bool operator==(const ScopeKey& other) const { return scope == other.scope && id == other.id; }
};

struct ScopeKeyHash
{
    size_t operator()(ScopeKey const& key) const
    {
        return (static_cast<size_t>(key.id) << 3) ^ static_cast<size_t>(key.scope);
    }
};

// The bot a particular person is in conversation with, and when it last answered
// them (epoch seconds). guid == 0 means nobody holds the floor.
//
// Deliberately not "the last bot to speak here": ambient remarks and event
// reactions from bots across the zone would steal the floor, and your follow-up
// would go to a bot that was never talking to you.
struct FloorHolder
{
    uint64_t guid  = 0;
    uint32_t atSec = 0;
};

// Build the key for a scope. Say is anchored to `counterpart` when supplied, or
// `actor` otherwise, so two distant conversations in one zone never share chat
// history or pacing. Whisper uses `counterpart`. A channel key includes its exact
// localized name and faction because General has the same numeric id in every zone.
ScopeKey MakeScope(ChatScope scope, Player* actor, Player* counterpart = nullptr,
                   uint32_t channelId = 0, const std::string& channelName = "");

// AzerothCore's public Player::IsInChannel helper only compares the numeric DBC
// id, which is shared by every localized General instance. Add the zone/city
// identity checks needed to tell whether this exact channel is one the player can
// actually hear. Custom channels are excluded because core exposes no exact
// public membership query for them.
bool IsInChannelInstance(Player* player, Channel const* channel);

// Human-readable name of a scope, for logs.
const char* ScopeName(ChatScope scope);

// Record a spoken line.
void RecordChatLine(const ScopeKey& key, uint64_t speakerGuid, const std::string& speakerName,
                    const std::string& text);

// Newest-last "Name: text" lines for the scope, capped by g_TranscriptLines and
// g_TranscriptMaxChars. Empty when nothing has been said there. A trailing line
// matching `excludeTrailing` is left out, so the message a bot is answering is
// not shown to it twice.
std::string RenderTranscript(const ScopeKey& key, const std::string& excludeTrailing = "");

// Note that `botGuid` just answered `humanGuid` here, giving it the floor.
void SetConversationFloor(const ScopeKey& key, uint64_t humanGuid, uint64_t botGuid);

// Which bot this person is currently talking to in this scope, if any.
FloorHolder GetConversationFloor(const ScopeKey& key, uint64_t humanGuid);

// Drop transcripts that have seen no traffic for a while. Called periodically so
// long sessions do not accumulate dead scopes.
void PruneTranscripts();

#endif // MOD_BOT_MINDS_TRANSCRIPT_H
