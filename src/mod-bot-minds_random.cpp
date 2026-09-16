#include "mod-bot-minds_random.h"
#include "mod-bot-minds_action.h"
#include "mod-bot-minds_config.h"
#include "mod-bot-minds_speak.h"
#include "mod-bot-minds_transcript.h"
#include "mod-bot-minds-utilities.h"

#include "Bag.h"
#include "CellImpl.h"
#include "Channel.h"
#include "ChannelMgr.h"
#include "Creature.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "QuestDef.h"
#include "Random.h"

#include <algorithm>
#include <ctime>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    struct AmbientSchedule
    {
        time_t dueAt = 0;
        time_t pausedAt = 0;
    };

    // One timer per audible scene, not per bot. A crowded inn should produce one
    // naturally spaced remark, not a wall of simultaneous requests from everyone
    // whose personal timer happened to expire on the same world tick.
    std::unordered_map<ScopeKey, AmbientSchedule, ScopeKeyHash> g_AmbientSchedules;

    // One passerby buff per person per cooldown, so kindness stays a nice surprise
    // rather than becoming a permanent automatic service.
    std::unordered_map<uint64_t, time_t> g_LastFavourAt;

    bool IsBot(Player* player)
    {
        PlayerbotAI* ai = PlayerbotsMgr::instance().GetPlayerbotAI(player);
        return ai && ai->IsBotAI();
    }

    Player* NearestRealPlayer(Player* bot, float maxDistance)
    {
        Player* nearest = nullptr;
        float nearestDistance = 0.0f;

        for (auto const& pair : ObjectAccessor::GetPlayers())
        {
            Player* candidate = pair.second;
            if (!candidate || IsBot(candidate) || !candidate->IsInWorld())
                continue;
            if (candidate->GetMapId() != bot->GetMapId())
                continue;

            float const distance = bot->GetDistance(candidate);
            if (distance > maxDistance)
                continue;
            if (!nearest || distance < nearestDistance)
            {
                nearest = candidate;
                nearestDistance = distance;
            }
        }

        return nearest;
    }

    // A group-mate always has an audience: party chat reaches the whole group
    // however far apart its members are.
    bool RealPlayerInGroup(Player* bot)
    {
        Group* group = bot->GetGroup();
        if (!group)
            return false;

        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member && member != bot && !IsBot(member))
                return true;
        }

        return false;
    }

    bool RealPlayerInChannel(Channel* channel)
    {
        if (!channel)
            return false;

        for (auto const& pair : ObjectAccessor::GetPlayers())
        {
            Player* candidate = pair.second;
            if (!candidate || IsBot(candidate) || !candidate->IsInWorld())
                continue;
            if (IsInChannelInstance(candidate, channel))
                return true;
        }

        return false;
    }

    Channel* ResolveChannel(Player* bot, uint32_t channelId)
    {
        ChannelMgr* manager = ChannelMgr::forTeam(bot->GetTeamId());
        if (!manager)
            return nullptr;

        for (auto const& entry : manager->GetChannels())
        {
            Channel* channel = entry.second;
            if (!channel || channel->GetChannelId() != channelId || channel->GetName().empty())
                continue;
            if (!IsInChannelInstance(bot, channel) || !RealPlayerInChannel(channel))
                continue;

            return channel;
        }

        return nullptr;
    }

    struct AmbientDestination
    {
        ChatScope scope = ChatScope::Say;
        uint32_t channelId = 0;
        std::string channelName;
        ScopeKey key;
    };

    std::vector<AmbientDestination> FindDestinations(Player* bot)
    {
        std::vector<AmbientDestination> destinations;

        if (RealPlayerInGroup(bot))
        {
            AmbientDestination destination;
            destination.scope = ChatScope::Party;
            destination.key = MakeScope(ChatScope::Party, bot);
            destinations.push_back(std::move(destination));
            return destinations;
        }

        if (Player* audience = NearestRealPlayer(bot, g_SayDistance))
        {
            AmbientDestination destination;
            destination.scope = ChatScope::Say;
            destination.key = MakeScope(ChatScope::Say, audience);
            destinations.push_back(std::move(destination));
        }

        auto addChannel = [&](uint32_t channelId, bool enabled)
        {
            if (!enabled)
                return;

            Channel* channel = ResolveChannel(bot, channelId);
            if (!channel)
                return;

            AmbientDestination option;
            option.scope = ChatScope::Channel;
            option.channelId = channel->GetChannelId();
            option.channelName = channel->GetName();
            option.key = MakeScope(ChatScope::Channel, bot, nullptr, option.channelId, option.channelName);
            destinations.push_back(std::move(option));
        };

        addChannel(ChatChannelId::GENERAL, g_AmbientUseGeneralChannel);
        addChannel(ChatChannelId::TRADE, g_AmbientUseTradeChannel);
        addChannel(ChatChannelId::LOOKING_FOR_GROUP, g_AmbientUseLfgChannel);
        addChannel(ChatChannelId::GUILD_RECRUITMENT, g_AmbientUseGuildRecruitmentChannel);

        return destinations;
    }

    // Passing players used to buff one another without stopping for a conversation.
    // Keep that separate from LLM chatter: choose a bot that genuinely has a
    // missing, castable buff and queue the spell directly on the world thread.
    void TryPasserbyBuffs(std::unordered_map<uint64_t, std::vector<Player*>>& nearbyBots,
                          time_t now, std::unordered_set<uint64_t>& usedBots)
    {
        if (!g_ActionsEnable || g_UnpromptedChance == 0)
            return;

        for (auto& entry : nearbyBots)
        {
            Player* audience = ObjectAccessor::FindPlayer(ObjectGuid(entry.first));
            if (!audience || !audience->IsInWorld() || !audience->IsAlive())
                continue;

            auto last = g_LastFavourAt.find(entry.first);
            if (last != g_LastFavourAt.end()
                && now < last->second + static_cast<time_t>(g_UnpromptedCooldownSec))
            {
                continue;
            }

            if (urand(0, 99) >= g_UnpromptedChance)
                continue;

            std::vector<Player*>& candidates = entry.second;
            std::shuffle(candidates.begin(), candidates.end(), RandomEngine::Instance());

            Player* chosenBot = nullptr;
            PasserbyBuffChoice chosenBuff;
            for (Player* bot : candidates)
            {
                if (!bot || usedBots.count(bot->GetGUID().GetRawValue()) != 0)
                    continue;

                ActionMenu const menu = BuildActionMenu(bot, audience, /*unprompted=*/true);
                PasserbyBuffChoice const candidate = ChoosePasserbyBuff(bot, audience, menu);
                if (candidate.score > chosenBuff.score)
                {
                    chosenBot = bot;
                    chosenBuff = candidate;
                }
            }

            if (!chosenBot)
                continue;

            BotAction action;
            action.kind = ActionKind::Buff;
            action.botGuid = chosenBot->GetGUID().GetRawValue();
            action.targetGuid = audience->GetGUID().GetRawValue();
            action.spellName = chosenBuff.spellName;
            SubmitBotAction(action);

            usedBots.insert(action.botGuid);
            g_LastFavourAt[entry.first] = now;

            if (g_DebugEnabled)
            {
                LOG_INFO("server.loading", "[BotMinds] {} chose passerby buff {} (score {}) for {}.",
                         chosenBot->GetName(), action.spellName, chosenBuff.score, audience->GetName());
            }
        }
    }

    // Nearest interesting thing around the bot, described plainly. The prompt
    // decides what to do with it.
    std::vector<std::string> ObserveSurroundings(Player* bot)
    {
        std::vector<std::string> observations;

        Unit* nearbyUnit = nullptr;
        {
            Acore::AnyUnitInObjectRangeCheck check(bot, g_SayDistance);
            Acore::UnitSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(bot, nearbyUnit, check);
            Cell::VisitObjects(bot, searcher, g_SayDistance);
        }

        if (nearbyUnit && nearbyUnit->GetTypeId() == TYPEID_UNIT)
        {
            Creature* creature = nearbyUnit->ToCreature();
            if (creature->HasNpcFlag(UNIT_NPC_FLAG_VENDOR))
                observations.push_back(SafeFormat("{} is selling wares nearby", creature->GetName()));
            else if (creature->HasNpcFlag(UNIT_NPC_FLAG_QUESTGIVER))
                observations.push_back(SafeFormat("{} is standing here with quests to give", creature->GetName()));
            else
                observations.push_back(SafeFormat("a {} is prowling around nearby", creature->GetName()));
        }

        {
            GameObject* nearbyObject = nullptr;
            Acore::GameObjectInRangeCheck check(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                                                g_SayDistance);
            Acore::GameObjectSearcher<Acore::GameObjectInRangeCheck> searcher(bot, nearbyObject, check);
            Cell::VisitObjects(bot, searcher, g_SayDistance);
            if (nearbyObject)
                observations.push_back(SafeFormat("there is a {} here", nearbyObject->GetName()));
        }

        {
            std::vector<Item*> equipped;
            for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
                if (Item* item = bot->GetItemByPos(slot))
                    equipped.push_back(item);

            if (!equipped.empty())
            {
                Item* item = equipped[urand(0, equipped.size() - 1)];
                observations.push_back(SafeFormat("you are using {}", item->GetTemplate()->Name1));
            }
        }

        {
            int freeSlots = 0;
            for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
                if (!bot->GetItemByPos(slot))
                    ++freeSlots;
            for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
                if (Bag* bag = bot->GetBagByPos(slot))
                    freeSlots += bag->GetFreeSlots();

            if (freeSlots <= 4)
                observations.push_back(SafeFormat("your bags are nearly full, {} slots left", freeSlots));
        }

        if (bot->GetMap() && bot->GetMap()->IsDungeon())
            observations.push_back(SafeFormat("you are inside {}", bot->GetMap()->GetMapName()));

        {
            std::vector<std::string> unfinished;
            for (auto const& status : bot->getQuestStatusMap())
            {
                if (status.second.Status != QUEST_STATUS_INCOMPLETE)
                    continue;
                if (Quest const* quest = sObjectMgr->GetQuestTemplate(status.first))
                    unfinished.push_back(quest->GetTitle());
            }

            if (!unfinished.empty())
                observations.push_back(SafeFormat("your quest {} is still unfinished",
                                                  unfinished[urand(0, unfinished.size() - 1)]));
        }

        return observations;
    }

    // A bot in somebody's group does not decide where it goes, so "say what you are
    // going to do next" invites it to announce an errand it will never run. Its own
    // words then contradict the fact that it is stood there following you.
    const char* PickAngle(bool underOrders)
    {
        static const char* anyone[] = {
            "complain about it",
            "make a dry observation",
            "ask the others what they think",
            "mention how the levelling is going",
            "say something a bit sarcastic"
        };

        static const char* freeToRoam[] = {
            "say what you are going to do next"
        };

        // One combined range, so a free bot keeps the fuller set of angles.
        const size_t extra = underOrders ? 0 : sizeof(freeToRoam) / sizeof(freeToRoam[0]);
        const size_t total = sizeof(anyone) / sizeof(anyone[0]) + extra;
        const size_t pick  = urand(0, total - 1);

        return pick < sizeof(anyone) / sizeof(anyone[0])
            ? anyone[pick]
            : freeToRoam[pick - sizeof(anyone) / sizeof(anyone[0])];
    }

    char const* ContextualGesture(Player* bot)
    {
        if (bot->HasRestFlag(REST_FLAG_IN_TAVERN))
            return "toast";
        if (bot->GetHealthPct() < 50.0f)
            return "sigh";
        if (bot->IsMounted())
            return "wave";
        if (bot->GetGroup())
            return urand(0, 1) == 0 ? "nod" : "salute";

        static char const* gestures[] = { "wave", "nod", "shrug" };
        return gestures[urand(0, sizeof(gestures) / sizeof(gestures[0]) - 1)];
    }
}

BotMindsAmbientChatter::BotMindsAmbientChatter() : WorldScript("BotMindsAmbientChatter") {}

void BotMindsAmbientChatter::OnUpdate(uint32 diff)
{
    if (!g_Enable || (!g_EnableAmbientChatter && !g_ActionsEnable))
        return;

    static uint32 timer = 0;
    if (timer > diff)
    {
        timer -= diff;
        return;
    }
    timer = 30000;

    const time_t now = time(nullptr);

    struct AmbientCandidate
    {
        Player* bot = nullptr;
        AmbientDestination destination;
    };

    std::unordered_map<ScopeKey, std::vector<AmbientCandidate>, ScopeKeyHash> scenes;
    std::unordered_map<uint64_t, std::vector<Player*>> nearbyBots;

    for (auto const& pair : ObjectAccessor::GetPlayers())
    {
        Player* bot = pair.second;
        if (!bot || !IsBot(bot) || !bot->IsInWorld() || bot->IsBeingTeleported())
            continue;

        bool const canAct = !g_DisableRepliesInCombat || !bot->IsInCombat();

        if (g_ActionsEnable && canAct)
        {
            if (Player* audience = NearestRealPlayer(bot, g_SayDistance))
                nearbyBots[audience->GetGUID().GetRawValue()].push_back(bot);
        }

        if (g_EnableAmbientChatter)
        {
            for (AmbientDestination& destination : FindDestinations(bot))
                scenes[destination.key].push_back({ bot, std::move(destination) });
        }
    }

    std::unordered_set<uint64_t> usedBots;
    TryPasserbyBuffs(nearbyBots, now, usedBots);

    if (!g_EnableAmbientChatter)
        return;

    // Forget scenes with no current audience. If they become active again later,
    // they receive a fresh interval rather than firing an overdue line at once.
    for (auto iterator = g_AmbientSchedules.begin(); iterator != g_AmbientSchedules.end();)
    {
        if (scenes.find(iterator->first) == scenes.end())
            iterator = g_AmbientSchedules.erase(iterator);
        else
            ++iterator;
    }

    std::vector<ScopeKey> sceneOrder;
    sceneOrder.reserve(scenes.size());
    for (auto const& entry : scenes)
        sceneOrder.push_back(entry.first);
    std::shuffle(sceneOrder.begin(), sceneOrder.end(), RandomEngine::Instance());

    for (ScopeKey const& sceneKey : sceneOrder)
    {
        std::vector<AmbientCandidate>& candidates = scenes[sceneKey];
        bool const hasAvailableBot = std::any_of(
            candidates.begin(), candidates.end(), [](AmbientCandidate const& candidate)
            {
                return candidate.bot && (!g_DisableRepliesInCombat || !candidate.bot->IsInCombat());
            });

        auto scheduled = g_AmbientSchedules.find(sceneKey);
        if (scheduled == g_AmbientSchedules.end())
        {
            AmbientSchedule schedule;
            schedule.dueAt = now + urand(g_AmbientMinIntervalSec, g_AmbientMaxIntervalSec);
            schedule.pausedAt = hasAvailableBot ? 0 : now;
            g_AmbientSchedules.emplace(sceneKey, schedule);
            continue;
        }

        AmbientSchedule& schedule = scheduled->second;
        if (!hasAvailableBot)
        {
            if (schedule.pausedAt == 0)
                schedule.pausedAt = now;
            continue;
        }

        if (schedule.pausedAt != 0)
        {
            schedule.dueAt += now - schedule.pausedAt;
            schedule.pausedAt = 0;
        }

        if (now < schedule.dueAt)
            continue;

        // A failed scene roll retries on the next 30-second coordinator tick.
        // Once somebody actually speaks, the full configured quiet interval begins.
        if (urand(0, 99) >= g_AmbientChance)
        {
            schedule.dueAt = now + 30;
            continue;
        }

        std::shuffle(candidates.begin(), candidates.end(), RandomEngine::Instance());

        auto chosen = std::find_if(candidates.begin(), candidates.end(), [&](AmbientCandidate const& candidate)
        {
            return candidate.bot && (!g_DisableRepliesInCombat || !candidate.bot->IsInCombat())
                && usedBots.count(candidate.bot->GetGUID().GetRawValue()) == 0;
        });
        if (chosen == candidates.end())
        {
            schedule.dueAt = now + 30;
            continue;
        }

        Player* bot = chosen->bot;
        AmbientDestination const& destination = chosen->destination;

        if (g_WorldLifeEnable && g_IdleGestureChance > 0
            && urand(0, 99) < g_IdleGestureChance)
        {
            Player* audience = NearestRealPlayer(bot, g_SayDistance);
            if (audience)
            {
                if (uint32_t const emote = ResolveEmote(
                    bot->GetGUID().GetRawValue(), ContextualGesture(bot)))
                {
                    SubmitBotEmote(bot->GetGUID().GetRawValue(), audience->GetGUID().GetRawValue(), emote);
                    usedBots.insert(bot->GetGUID().GetRawValue());
                    schedule.dueAt = now + urand(g_AmbientMinIntervalSec, g_AmbientMaxIntervalSec);
                    continue;
                }
            }
        }

        std::vector<std::string> observations = ObserveSurroundings(bot);
        std::string situation = observations.empty()
            ? "nothing much is happening"
            : observations[urand(0, observations.size() - 1)];

        situation = SafeFormat("{}. Take this angle: {}.", situation,
                               PickAngle(destination.scope == ChatScope::Party));

        TurnRequest request;
        request.bot     = bot;
        request.kind    = TurnKind::Ambient;
        request.key     = destination.key;
        request.trigger = situation;
        request.channelName = destination.channelName;

        if (RequestBotTurn(request, /*priority=*/false))
        {
            usedBots.insert(bot->GetGUID().GetRawValue());
            schedule.dueAt = now + urand(g_AmbientMinIntervalSec, g_AmbientMaxIntervalSec);
            if (g_DebugEnabled)
            {
                LOG_INFO("server.loading", "[BotMinds] Ambient {} scene chose {} from {} eligible bot(s).",
                         ScopeName(destination.scope), bot->GetName(), candidates.size());
            }
        }
        else
        {
            schedule.dueAt = now + 30;
        }
    }
}
