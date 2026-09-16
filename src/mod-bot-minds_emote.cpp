#include "mod-bot-minds_emote.h"
#include "mod-bot-minds_action.h"
#include "mod-bot-minds_config.h"
#include "mod-bot-minds_speak.h"
#include "mod-bot-minds_transcript.h"
#include "mod-bot-minds-utilities.h"

#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "Random.h"
#include "SharedDefines.h"
#include "WorldSession.h"

#include <ctime>
#include <string>
#include <unordered_map>

namespace
{
    std::unordered_map<uint64_t, std::unordered_map<uint64_t, time_t>> g_LastReaction;

    bool IsBot(Player* player)
    {
        if (!player)
            return false;
        if (player->GetSession() && player->GetSession()->IsBot())
            return true;
        PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(player);
        return botAI && botAI->IsBotAI();
    }

    bool ConsumeCooldown(Player* bot, Player* player)
    {
        time_t const now = time(nullptr);
        time_t& last = g_LastReaction[bot->GetGUID().GetRawValue()][player->GetGUID().GetRawValue()];
        if (last != 0 && now - last < static_cast<time_t>(g_EmoteReactionCooldownSec))
            return false;
        last = now;
        return true;
    }

    uint32_t MirrorEmote(uint32_t inbound)
    {
        switch (inbound)
        {
            case TEXT_EMOTE_WAVE:         return TEXT_EMOTE_WAVE;
            case TEXT_EMOTE_HELLO:        return TEXT_EMOTE_HELLO;
            case TEXT_EMOTE_GREET:        return TEXT_EMOTE_GREET;
            case TEXT_EMOTE_HAIL:         return TEXT_EMOTE_WAVE;
            case TEXT_EMOTE_BOW:          return TEXT_EMOTE_BOW;
            case TEXT_EMOTE_CURTSEY:      return TEXT_EMOTE_BOW;
            case TEXT_EMOTE_SALUTE:       return TEXT_EMOTE_SALUTE;
            case TEXT_EMOTE_NOD:          return TEXT_EMOTE_NOD;
            case TEXT_EMOTE_AGREE:        return TEXT_EMOTE_AGREE;
            case TEXT_EMOTE_CHEER:        return TEXT_EMOTE_CHEER;
            case TEXT_EMOTE_APPLAUD:      return TEXT_EMOTE_CLAP;
            case TEXT_EMOTE_CLAP:         return TEXT_EMOTE_CLAP;
            case TEXT_EMOTE_LAUGH:        return TEXT_EMOTE_CHUCKLE;
            case TEXT_EMOTE_DANCE:        return TEXT_EMOTE_DANCE;
            case TEXT_EMOTE_THANK:        return TEXT_EMOTE_BOW;
            case TEXT_EMOTE_CONGRATULATE: return TEXT_EMOTE_THANK;
            case TEXT_EMOTE_HUG:          return TEXT_EMOTE_HUG;
            case TEXT_EMOTE_HIGHFIVE:     return TEXT_EMOTE_HIGHFIVE;
            case TEXT_EMOTE_TOAST:        return TEXT_EMOTE_TOAST;
            case TEXT_EMOTE_BYE:          return TEXT_EMOTE_WAVE;
            default:                      return 0;
        }
    }

    uint32_t CounterEmote(uint32_t inbound)
    {
        switch (inbound)
        {
            case TEXT_EMOTE_FLEX:      return TEXT_EMOTE_LAUGH;
            case TEXT_EMOTE_RUDE:      return TEXT_EMOTE_GLARE;
            case TEXT_EMOTE_SPIT:      return TEXT_EMOTE_ANGRY;
            case TEXT_EMOTE_INSULT:    return TEXT_EMOTE_SNARL;
            case TEXT_EMOTE_MOCK:      return TEXT_EMOTE_FROWN;
            case TEXT_EMOTE_TAUNT:     return TEXT_EMOTE_THREATEN;
            case TEXT_EMOTE_THREATEN:  return TEXT_EMOTE_BRANDISH;
            case TEXT_EMOTE_CHALLENGE: return TEXT_EMOTE_READY;
            case TEXT_EMOTE_POKE:      return TEXT_EMOTE_EYEBROW;
            case TEXT_EMOTE_TICKLE:    return TEXT_EMOTE_GIGGLE;
            case TEXT_EMOTE_KISS:      return TEXT_EMOTE_BLUSH;
            case TEXT_EMOTE_FLIRT:     return TEXT_EMOTE_BASHFUL;
            case TEXT_EMOTE_CRY:       return TEXT_EMOTE_COMFORT;
            case TEXT_EMOTE_MOURN:     return TEXT_EMOTE_COMFORT;
            case TEXT_EMOTE_PLEAD:     return TEXT_EMOTE_PONDER;
            case TEXT_EMOTE_BEG:       return TEXT_EMOTE_SHRUG;
            case TEXT_EMOTE_HELPME:    return TEXT_EMOTE_READY;
            case TEXT_EMOTE_OOM:       return TEXT_EMOTE_NOD;
            case TEXT_EMOTE_PONDER:    return TEXT_EMOTE_SHRUG;
            case TEXT_EMOTE_STARE:     return TEXT_EMOTE_EYEBROW;
            case TEXT_EMOTE_ROAR:      return TEXT_EMOTE_CHEER;
            default:                   return 0;
        }
    }

    char const* EmoteDescription(uint32_t emote)
    {
        switch (emote)
        {
            case TEXT_EMOTE_WAVE:
            case TEXT_EMOTE_HELLO:
            case TEXT_EMOTE_GREET:
            case TEXT_EMOTE_HAIL:         return "waved at you";
            case TEXT_EMOTE_BOW:
            case TEXT_EMOTE_CURTSEY:      return "bowed to you";
            case TEXT_EMOTE_SALUTE:       return "saluted you";
            case TEXT_EMOTE_NOD:
            case TEXT_EMOTE_AGREE:        return "nodded at you";
            case TEXT_EMOTE_CHEER:
            case TEXT_EMOTE_APPLAUD:
            case TEXT_EMOTE_CLAP:         return "cheered for you";
            case TEXT_EMOTE_LAUGH:        return "laughed at you";
            case TEXT_EMOTE_DANCE:        return "danced with you";
            case TEXT_EMOTE_THANK:        return "thanked you";
            case TEXT_EMOTE_CONGRATULATE: return "congratulated you";
            case TEXT_EMOTE_HUG:          return "hugged you";
            case TEXT_EMOTE_HIGHFIVE:     return "offered you a high five";
            case TEXT_EMOTE_TOAST:        return "toasted you";
            case TEXT_EMOTE_BYE:          return "said goodbye to you";
            case TEXT_EMOTE_FLEX:         return "flexed at you";
            case TEXT_EMOTE_RUDE:         return "made a rude gesture at you";
            case TEXT_EMOTE_SPIT:         return "spat at you";
            case TEXT_EMOTE_INSULT:       return "insulted you";
            case TEXT_EMOTE_MOCK:
            case TEXT_EMOTE_TAUNT:        return "taunted you";
            case TEXT_EMOTE_THREATEN:     return "threatened you";
            case TEXT_EMOTE_CHALLENGE:    return "challenged you";
            case TEXT_EMOTE_POKE:         return "poked you";
            case TEXT_EMOTE_TICKLE:       return "tickled you";
            case TEXT_EMOTE_KISS:         return "kissed you";
            case TEXT_EMOTE_FLIRT:        return "flirted with you";
            case TEXT_EMOTE_CRY:
            case TEXT_EMOTE_MOURN:        return "cried in front of you";
            case TEXT_EMOTE_PLEAD:
            case TEXT_EMOTE_BEG:          return "pleaded with you";
            case TEXT_EMOTE_HELPME:       return "asked you for help";
            case TEXT_EMOTE_OOM:          return "told you they were out of mana";
            case TEXT_EMOTE_PONDER:       return "pondered at you";
            case TEXT_EMOTE_STARE:        return "stared at you";
            case TEXT_EMOTE_ROAR:         return "roared at you";
            default:                      return "used an emote at you";
        }
    }
}

BotMindsOnEmote::BotMindsOnEmote() : PlayerScript("BotMindsOnEmote") {}

void BotMindsOnEmote::OnPlayerTextEmote(Player* player, uint32 textEmote,
                                        uint32 /*emoteNum*/, ObjectGuid targetGuid)
{
    if (!g_Enable || !g_EnableEmoteReactions || !player || targetGuid.IsEmpty() || IsBot(player))
        return;

    Player* bot = ObjectAccessor::FindConnectedPlayer(targetGuid);
    if (!bot || bot == player || !bot->IsInWorld() || !IsBot(bot))
        return;
    if (g_DisableRepliesInCombat && bot->IsInCombat())
        return;
    if (bot->GetMapId() != player->GetMapId()
        || (g_SayDistance > 0.0f && bot->GetDistance(player) > g_SayDistance))
        return;
    if (!ConsumeCooldown(bot, player) || urand(0, 99) >= g_EmoteReactionChance)
        return;

    uint32_t const totalWeight = g_EmoteReactionMirrorWeight + g_EmoteReactionCounterWeight
        + g_EmoteReactionSpeakWeight;
    if (totalWeight == 0)
        return;

    uint32_t const roll = urand(0, totalWeight - 1);
    uint32_t responseEmote = 0;
    bool speak = false;

    if (roll < g_EmoteReactionMirrorWeight)
        responseEmote = MirrorEmote(textEmote);
    else if (roll < g_EmoteReactionMirrorWeight + g_EmoteReactionCounterWeight)
        responseEmote = CounterEmote(textEmote);
    else
        speak = true;

    if (!speak && responseEmote == 0)
        responseEmote = MirrorEmote(textEmote);
    if (!speak && responseEmote == 0)
        responseEmote = CounterEmote(textEmote);
    if (!speak && responseEmote == 0)
        speak = true;

    if (responseEmote != 0)
    {
        SubmitBotEmote(bot->GetGUID().GetRawValue(), player->GetGUID().GetRawValue(), responseEmote);
        if (g_DebugEnabled)
        {
            LOG_INFO("server.loading", "[BotMinds] {} reacted to emote {} from {} with emote {}.",
                     bot->GetName(), textEmote, player->GetName(), responseEmote);
        }
        return;
    }

    TurnRequest request;
    request.bot = bot;
    request.other = player;
    request.kind = TurnKind::EmoteReaction;
    request.key = MakeScope(ChatScope::Say, player);
    request.trigger = SafeFormat("{} {}", player->GetName(), EmoteDescription(textEmote));
    request.namedDirectly = true;
    request.replyRequired = true;
    RequestBotTurn(request, /*priority=*/true);
}
