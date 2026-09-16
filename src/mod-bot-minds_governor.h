#ifndef MOD_BOT_MINDS_GOVERNOR_H
#define MOD_BOT_MINDS_GOVERNOR_H

#include "mod-bot-minds_transcript.h"

#include <cstdint>
#include <string>

class Player;

// --------------------------------------------
// Resource limits on LLM calls: a usable provider, the per-minute ceiling, the
// concurrency slots, the per-bot cooldown and proximity.
//
// Whether a bot *wants* to speak is decided in _attention.*; the governor only
// decides whether it *may*. Keeping probability out of here is deliberate: two
// independent chance rolls in two layers is what used to make bots answer at
// random.
// --------------------------------------------
namespace BotMindsGovernor
{
    // `priority` marks a turn addressed to this bot. It skips conversational
    // cooldown and proximity gates, but still respects the hard cap and the
    // concurrency limit. Whether the model must answer is a separate decision.
    bool Allow(Player* bot, Player* addresser, ScopeKey const& key, bool priority);

    // Called by the dispatcher after a request is accepted: sets the bot's
    // cooldown, ++in-flight, ++calls-this-interval.
    void OnSubmit(uint64_t botGuid, ScopeKey const& key);

    // Call when a call completes, fails or is abandoned: --in-flight. Always pair
    // with OnSubmit.
    void OnComplete();

    // Call from a WorldScript OnUpdate: rolls the per-minute counter over.
    void Tick(uint32_t diffMs);

    // Calls submitted since startup, and how many are in flight right now, for
    // `.botminds status`. Every call costs money, so it should be countable.
    uint32_t CallsSinceStartup();
    int      CallsInFlight();

    // Delivery-time repetition guard. A generated line is compared with recent
    // lines from both this bot and this conversation scope before it is spoken.
    bool IsRepetitive(uint64_t botGuid, ScopeKey const& key, std::string const& text);
    void RecordUtterance(uint64_t botGuid, ScopeKey const& key, std::string const& text);

    uint32_t RepetitionsBlocked();
    uint32_t PacingBlocked();
}

#endif // MOD_BOT_MINDS_GOVERNOR_H
