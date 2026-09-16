#ifndef MOD_BOT_MINDS_EMOTE_H
#define MOD_BOT_MINDS_EMOTE_H

#include "ScriptMgr.h"

// Reacts only when a real player deliberately aims a text emote at a bot.
// Gesture reactions stay local and free; the configured speak share uses the
// normal Bot Minds turn pipeline.
class BotMindsOnEmote : public PlayerScript
{
public:
    BotMindsOnEmote();
    void OnPlayerTextEmote(Player* player, uint32 textEmote, uint32 emoteNum,
                           ObjectGuid targetGuid) override;
};

#endif // MOD_BOT_MINDS_EMOTE_H
