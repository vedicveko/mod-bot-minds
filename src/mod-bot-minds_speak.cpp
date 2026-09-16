#include "mod-bot-minds_speak.h"
#include "mod-bot-minds_action.h"
#include "mod-bot-minds_attention.h"
#include "mod-bot-minds_config.h"
#include "mod-bot-minds_dispatch.h"
#include "mod-bot-minds_governor.h"
#include "mod-bot-minds_llmclient.h"
#include "mod-bot-minds_memory.h"
#include "mod-bot-minds_relationship.h"
#include "mod-bot-minds_transcript.h"

#include "Channel.h"
#include "ChannelMgr.h"
#include "Group.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace
{
    struct BotTurnContext
    {
        uint64_t botGuid = 0;
        uint64_t otherGuid = 0;
        bool otherIsBot = false;
        bool namedDirectly = false;
        bool replyRequired = false;
        std::string botName;
        std::string whisperTarget;
        ScopeKey key;
        std::string channelName;
        uint32_t chainDepth = 0;
        TurnKind kind = TurnKind::DirectReply;
        ActionMenu menu;
    };

    void StripNamePrefix(std::string& reply, std::string const& name)
    {
        auto equalIgnoringCase = [](std::string const& left, std::string const& right)
        {
            if (left.size() != right.size())
                return false;
            for (size_t index = 0; index < left.size(); ++index)
            {
                if (std::tolower(static_cast<unsigned char>(left[index]))
                    != std::tolower(static_cast<unsigned char>(right[index])))
                {
                    return false;
                }
            }
            return true;
        };

        if (reply.size() >= 3 && (reply.front() == '<' || reply.front() == '['))
        {
            char const closer = reply.front() == '<' ? '>' : ']';
            size_t const end = reply.find(closer);
            if (end != std::string::npos && equalIgnoringCase(reply.substr(1, end - 1), name))
            {
                size_t marker = end + 1;
                while (marker < reply.size() && reply[marker] == ' ')
                    ++marker;
                if (marker < reply.size() && (reply[marker] == ':' || reply[marker] == '-'))
                    ++marker;
                reply.erase(0, marker);
            }
        }
        else if (reply.size() > name.size() && equalIgnoringCase(reply.substr(0, name.size()), name))
        {
            size_t marker = name.size();
            while (marker < reply.size() && reply[marker] == ' ')
                ++marker;
            if (marker < reply.size() && (reply[marker] == ':' || reply[marker] == '-'))
                reply.erase(0, marker + 1);
        }

        size_t const start = reply.find_first_not_of(' ');
        reply = start == std::string::npos ? "" : reply.substr(start);
    }

    bool RealPlayerWithinSayRange(Player* bot)
    {
        for (auto const& pair : ObjectAccessor::GetPlayers())
        {
            Player* other = pair.second;
            if (!other || other == bot || !other->IsInWorld())
                continue;
            if (PlayerbotsMgr::instance().GetPlayerbotAI(other))
                continue;
            if (bot->GetMapId() == other->GetMapId() && bot->GetDistance(other) <= g_SayDistance)
                return true;
        }
        return false;
    }

    bool SpeakInScope(Player* bot, PlayerbotAI* botAI, std::string const& reply,
                      ChatScope scope, std::string const& channelName, std::string const& whisperTarget)
    {
        switch (scope)
        {
            case ChatScope::Say:
                if (!RealPlayerWithinSayRange(bot))
                    return false;
                botAI->Say(reply);
                return true;
            case ChatScope::Party:
                if (!bot->GetGroup())
                    return false;
                if (bot->GetGroup()->isRaidGroup())
                    botAI->SayToRaid(reply);
                else
                    botAI->SayToParty(reply);
                return true;
            case ChatScope::Guild:
                if (!bot->GetGuild())
                    return false;
                botAI->SayToGuild(reply);
                return true;
            case ChatScope::Whisper:
                if (whisperTarget.empty())
                    return false;
                botAI->Whisper(reply, whisperTarget);
                return true;
            case ChatScope::Channel:
            {
                ChannelMgr* manager = ChannelMgr::forTeam(bot->GetTeamId());
                if (!manager)
                    return false;

                Channel* channel = manager->GetChannel(channelName, bot);
                if (!IsInChannelInstance(bot, channel))
                    return false;

                channel->Say(bot->GetGUID(), reply, LANG_UNIVERSAL);
                return true;
            }
        }

        return false;
    }

    void CompleteBotTurn(BotTurnContext const& context, LLMResult result)
    {
        bool const declined = !result.shouldReply && !context.replyRequired;
        if (!result.ok || declined || result.reply.empty())
        {
            if (g_DebugEnabled)
            {
                std::string const reason = !result.ok
                    ? (result.error.empty() ? "no usable response" : result.error)
                    : "chose not to reply";
                LOG_INFO("server.loading", "[BotMinds] {} stayed silent ({}).", context.botName, reason);
            }
            return;
        }

        StripNamePrefix(result.reply, context.botName);
        if (result.reply.empty())
            return;

        if (BotMindsGovernor::IsRepetitive(context.botGuid, context.key, result.reply))
        {
            if (g_DebugEnabled)
            {
                LOG_INFO("server.loading", "[BotMinds] {} repeated a recent line; reply dropped.",
                         context.botName);
            }
            return;
        }

        Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(context.botGuid));
        if (!bot || !bot->IsInWorld())
            return;

        PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(bot);
        if (!botAI)
            return;

        if (!SpeakInScope(bot, botAI, result.reply, context.key.scope,
                          context.channelName, context.whisperTarget))
        {
            if (g_DebugEnabled)
            {
                LOG_INFO("server.loading", "[BotMinds] {} could not speak in {}; line dropped.",
                         context.botName, ScopeName(context.key.scope));
            }
            return;
        }

        if (g_DebugEnabled)
        {
            LOG_INFO("server.loading", "[BotMinds] {} ({} in {}): {}", context.botName,
                     static_cast<int>(context.kind), ScopeName(context.key.scope), result.reply);
        }

        if (!context.otherIsBot && context.otherGuid != 0 && !bot->IsInCombat()
            && !(bot->GetGroup() && bot->GetGroup()->IsMember(ObjectGuid(context.otherGuid))))
        {
            HoldStillForConversation(context.botGuid, context.otherGuid);
        }

        RecordChatLine(context.key, context.botGuid, context.botName, result.reply);
        BotMindsGovernor::RecordUtterance(context.botGuid, context.key, result.reply);

        if (context.otherGuid != 0)
            RecordInteraction(context.botGuid, context.otherGuid, context.otherIsBot);

        if (!context.otherIsBot && context.otherGuid != 0
            && (context.kind == TurnKind::DirectReply || context.kind == TurnKind::Interjection
                || context.kind == TurnKind::EmoteReaction))
        {
            SetConversationFloor(context.key, context.otherGuid, context.botGuid);
        }

        std::vector<PendingMemory> memories;
        if (context.kind != TurnKind::Ambient && result.memory_additions.is_array())
        {
            for (auto const& entry : result.memory_additions)
            {
                if (!entry.is_object() || !entry.contains("text") || !entry["text"].is_string())
                    continue;

                std::string text = entry["text"].get<std::string>();
                if (text.empty())
                    continue;

                PendingMemory memory;
                memory.kind = entry.contains("kind") && entry["kind"].is_string()
                    ? entry["kind"].get<std::string>() : "event";
                memory.text = std::move(text);
                memory.salience = entry.contains("salience") && entry["salience"].is_number()
                    ? entry["salience"].get<float>() : 0.5f;
                memories.push_back(std::move(memory));
            }
        }

        if (g_DebugEnabled && context.kind != TurnKind::Ambient && memories.empty())
            LOG_INFO("server.loading", "[BotMinds] {} came back with nothing to remember.", context.botName);

        bool const hasRelationshipChange = context.otherGuid != 0
            && result.relationship_delta.is_object()
            && result.relationship_delta.contains("affinity_change")
            && result.relationship_delta["affinity_change"].is_number();
        float const affinityChange = hasRelationshipChange
            ? result.relationship_delta["affinity_change"].get<float>() : 0.0f;
        std::string const affinityReason = hasRelationshipChange
            && result.relationship_delta.contains("reason") && result.relationship_delta["reason"].is_string()
            ? result.relationship_delta["reason"].get<std::string>() : std::string();

        if (!result.emote.empty() && !context.otherIsBot && context.otherGuid != 0)
        {
            if (uint32_t const emoteId = ResolveEmote(context.botGuid, result.emote))
            {
                BotAction gesture;
                gesture.kind = ActionKind::Emote;
                gesture.botGuid = context.botGuid;
                gesture.targetGuid = context.otherGuid;
                gesture.emoteId = emoteId;
                SubmitBotAction(gesture);
            }
        }

        bool committed = false;
        if (result.action.is_object())
        {
            BotAction action;
            action.botGuid = context.botGuid;
            action.targetGuid = context.otherGuid;
            std::string const actionKind = result.action.contains("kind") && result.action["kind"].is_string()
                ? result.action["kind"].get<std::string>() : "none";
            action.kind = ActionKindFromName(actionKind);
            if (result.action.contains("spell") && result.action["spell"].is_string())
                action.spellName = result.action["spell"].get<std::string>();
            if (result.action.contains("copper") && result.action["copper"].is_number_unsigned())
            {
                uint64_t const copper = result.action["copper"].get<uint64_t>();
                action.copper = static_cast<uint32_t>(std::min<uint64_t>(
                    copper, std::numeric_limits<uint32_t>::max()));
            }
            else if (result.action.contains("copper") && result.action["copper"].is_number_integer())
            {
                int64_t const copper = result.action["copper"].get<int64_t>();
                if (copper > 0)
                {
                    action.copper = static_cast<uint32_t>(std::min<uint64_t>(
                        static_cast<uint64_t>(copper), std::numeric_limits<uint32_t>::max()));
                }
            }
            action.promised = context.kind == TurnKind::DirectReply || context.kind == TurnKind::Interjection;

            std::string lowered = result.reply;
            for (char& character : lowered)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));

            for (char const* hint : {"mail", "post", "inbox", "sent it", "send it"})
            {
                if (lowered.find(hint) != std::string::npos)
                {
                    action.mentionedPost = true;
                    break;
                }
            }

            action.wholeGroup = !context.namedDirectly
                && context.key.scope == ChatScope::Party
                && (action.kind == ActionKind::Follow || action.kind == ActionKind::Stay);

            if (action.kind != ActionKind::None && ValidateAction(context.menu, action))
            {
                action.memories = std::move(memories);
                action.otherIsBot = context.otherIsBot;
                action.hasRelationshipChange = hasRelationshipChange;
                action.affinityChange = affinityChange;
                action.affinityReason = affinityReason;

                SubmitBotAction(action);
                memories.clear();
                committed = true;
            }
            else if (g_DebugEnabled && action.kind != ActionKind::None)
            {
                LOG_INFO("server.loading",
                         "[BotMinds] Dropped an action {} tried to take that was not on offer.",
                         context.botName);
            }
        }

        if (!committed)
        {
            for (PendingMemory const& memory : memories)
            {
                AddMemory(context.botGuid, context.otherGuid, memory.kind, memory.text, memory.salience);
            }

            if (hasRelationshipChange)
            {
                ApplyRelationshipDelta(context.botGuid, context.otherGuid, context.otherIsBot,
                                       affinityChange, affinityReason);
            }
        }

        OnLineSpoken(context.botGuid, context.botName, result.reply, context.key,
                     context.channelName, context.chainDepth);
    }
}

bool RequestBotTurn(TurnRequest& request, bool priority)
{
    if (!g_Enable || !request.bot)
        return false;
    if (g_DisableRepliesInCombat && request.bot->IsInCombat())
        return false;
    if (request.key.scope == ChatScope::Say && !RealPlayerWithinSayRange(request.bot))
        return false;
    if (!BotMindsGovernor::Allow(request.bot, request.other, request.key, priority))
        return false;

    TurnPrompt prompt = BuildTurnPrompt(request);
    if (prompt.system.empty())
        return false;

    LLMProviderPtr provider = GetProvider();
    if (!provider)
        return false;

    BotTurnContext context;
    context.botGuid = request.bot->GetGUID().GetRawValue();
    context.otherGuid = request.other ? request.other->GetGUID().GetRawValue() : 0;
    context.otherIsBot = request.other
        && PlayerbotsMgr::instance().GetPlayerbotAI(request.other) != nullptr;
    context.namedDirectly = request.namedDirectly;
    context.replyRequired = request.replyRequired;
    context.botName = request.bot->GetName();
    context.whisperTarget = request.other ? request.other->GetName() : "";
    context.key = request.key;
    context.channelName = request.channelName;
    context.chainDepth = request.chainDepth;
    context.kind = request.kind;
    context.menu = request.menu;

    bool const holdForConversation = priority && request.key.scope == ChatScope::Say
        && request.other && !context.otherIsBot && !request.bot->IsInCombat()
        && !(request.bot->GetGroup() && request.bot->GetGroup()->IsMember(request.other->GetGUID()));
    uint64_t const holdBotGuid = context.botGuid;
    uint64_t const holdTargetGuid = context.otherGuid;

    BotMindsDispatchRequest dispatch;
    dispatch.provider = std::move(provider);
    dispatch.systemPrompt = std::move(prompt.system);
    dispatch.userPrompt = std::move(prompt.user);
    dispatch.label = context.botName;
    dispatch.governorBotGuid = context.botGuid;
    dispatch.governorScopeKey = context.key;
    dispatch.holdsGovernorSlot = true;
    dispatch.simulateTyping = g_EnableTypingSimulation;
    dispatch.typingBaseDelayMs = g_TypingSimulationBaseDelay;
    dispatch.typingDelayPerCharMs = g_TypingSimulationDelayPerChar;
    dispatch.typingMaxDelayMs = g_TypingSimulationMaxDelay;
    dispatch.onComplete = [context = std::move(context)](LLMResult&& result)
    {
        CompleteBotTurn(context, std::move(result));
    };

    if (!BotMindsDispatch_Submit(std::move(dispatch)))
        return false;

    // A player should not lose a local conversation because the bot wandered out
    // of earshot while the provider was thinking or the typing delay was running.
    // The existing hold is refreshed after delivery, so normal AI resumes after
    // the configured quiet period exactly as before.
    if (holdForConversation)
    {
        HoldStillForConversation(holdBotGuid, holdTargetGuid);
        if (g_DebugEnabled)
        {
            LOG_INFO("server.loading", "[BotMinds] {} paused for conversation while its reply is prepared.",
                     request.bot->GetName());
        }
    }

    return true;
}
