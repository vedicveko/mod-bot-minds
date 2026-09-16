#include "mod-bot-minds_events.h"
#include "mod-bot-minds_action.h"
#include "mod-bot-minds_config.h"
#include "mod-bot-minds_memory.h"
#include "mod-bot-minds_persona.h"
#include "mod-bot-minds_relationship.h"
#include "mod-bot-minds_speak.h"
#include "mod-bot-minds_transcript.h"
#include "mod-bot-minds-utilities.h"

#include "Creature.h"
#include "AchievementMgr.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "Group.h"
#include "Guild.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "QuestDef.h"
#include "Random.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldSession.h"

#include <algorithm>
#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    std::unordered_map<uint64_t, uint32_t> g_LastKnownZone;
    std::unordered_map<uint64_t, uint32_t> g_LastKnownMap;
    std::unordered_map<uint64_t, time_t> g_LastJourneyAt;
    std::unordered_map<uint64_t, std::unordered_map<uint64_t, time_t>> g_LastReunionAt;

    bool IsBot(Player* player)
    {
        if (player && player->GetSession() && player->GetSession()->IsBot())
            return true;
        PlayerbotAI* ai = PlayerbotsMgr::instance().GetPlayerbotAI(player);
        return ai && ai->IsBotAI();
    }

    Player* LocalAudience(Player* actor)
    {
        if (!IsBot(actor))
            return actor;

        Player* nearest = nullptr;
        float nearestDistance = 0.0f;

        for (auto const& pair : ObjectAccessor::GetPlayers())
        {
            Player* candidate = pair.second;
            if (!candidate || IsBot(candidate) || !candidate->IsInWorld())
                continue;
            if (candidate->GetMapId() != actor->GetMapId())
                continue;

            float const distance = candidate->GetDistance(actor);
            if (distance > g_EventDistance)
                continue;
            if (!nearest || distance < nearestDistance)
            {
                nearest = candidate;
                nearestDistance = distance;
            }
        }
        return nearest;
    }

    bool RealPlayerInGuild(uint32 guildId)
    {
        if (guildId == 0)
            return false;

        for (auto const& pair : ObjectAccessor::GetPlayers())
        {
            Player* candidate = pair.second;
            if (!candidate || IsBot(candidate) || !candidate->IsInWorld())
                continue;
            if (candidate->GetGuildId() == guildId)
                return true;
        }
        return false;
    }

    Player* FirstRealPlayer(Group* group)
    {
        if (!group)
            return nullptr;

        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member && member->IsInWorld() && !IsBot(member))
                return member;
        }

        return nullptr;
    }

    std::vector<Player*> GroupBots(Group* group)
    {
        std::vector<Player*> bots;
        if (!group)
            return bots;

        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !member->IsInWorld() || member->IsBeingTeleported() || !IsBot(member))
                continue;
            bots.push_back(member);
        }

        return bots;
    }

    bool SharesJourney(Player* actor, Player* member)
    {
        if (!actor || !member || actor->GetMapId() != member->GetMapId())
            return false;
        return actor->GetZoneId() == member->GetZoneId()
            || actor->GetDistance(member) <= g_EventDistance;
    }

    Player* PresentRealPlayer(Player* actor)
    {
        if (!actor || !actor->GetGroup())
            return nullptr;
        if (!IsBot(actor))
            return actor;

        for (GroupReference* ref = actor->GetGroup()->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member && !IsBot(member) && SharesJourney(actor, member))
                return member;
        }

        return nullptr;
    }

    std::vector<Player*> JourneyBots(Player* actor)
    {
        std::vector<Player*> bots = GroupBots(actor ? actor->GetGroup() : nullptr);
        bots.erase(std::remove_if(bots.begin(), bots.end(), [actor](Player* bot)
        {
            return !SharesJourney(actor, bot);
        }), bots.end());
        return bots;
    }

    bool JourneyReady(Player* player)
    {
        time_t const now = time(nullptr);
        uint64_t const journeyId = player->GetGroup()
            ? player->GetGroup()->GetGUID().GetRawValue() : player->GetGUID().GetRawValue();
        time_t& last = g_LastJourneyAt[journeyId];
        if (last != 0 && now - last < static_cast<time_t>(g_JourneyCooldownSec))
            return false;
        last = now;
        return true;
    }

    std::string AreaName(uint32 areaId)
    {
        AreaTableEntry const* area = sAreaTableStore.LookupEntry(areaId);
        return area && area->area_name[0] ? area->area_name[0] : "somewhere new";
    }

    bool IsTown(uint32 areaId)
    {
        AreaTableEntry const* area = sAreaTableStore.LookupEntry(areaId);
        return area && (area->flags & (AREA_FLAG_CAPITAL | AREA_FLAG_CITY | AREA_FLAG_TOWN));
    }

    void RememberSharedExperience(Player* actor, std::string const& description,
                                  float salience, float affinityChange)
    {
        if (!g_WorldLifeEnable)
            return;

        Group* group = actor ? actor->GetGroup() : nullptr;
        Player* realPlayer = PresentRealPlayer(actor);
        if (!group || !realPlayer)
            return;

        std::vector<Player*> bots = JourneyBots(actor);
        std::string const memory = SafeFormat("Your group shared this: {}", description);
        for (Player* bot : bots)
        {
            AddMemory(bot->GetGUID().GetRawValue(), realPlayer->GetGUID().GetRawValue(),
                      "event", memory, salience);
        }

        if (affinityChange <= 0.0f)
            return;

        std::string const reason = SafeFormat("shared an adventure: {}", description);
        for (size_t left = 0; left < bots.size(); ++left)
        {
            for (size_t right = left + 1; right < bots.size(); ++right)
            {
                uint64_t const leftGuid = bots[left]->GetGUID().GetRawValue();
                uint64_t const rightGuid = bots[right]->GetGUID().GetRawValue();
                ApplyRelationshipDelta(leftGuid, rightGuid, true, affinityChange, reason);
                ApplyRelationshipDelta(rightGuid, leftGuid, true, affinityChange, reason);
            }
        }
    }

    bool DispatchGroupEvent(Player* actor, std::string const& description, uint32_t chance,
                            bool remember, float salience, float affinityChange,
                            std::string const& gesture = "")
    {
        if (!g_Enable || !g_WorldLifeEnable || !actor || !actor->GetGroup())
            return false;

        Player* realPlayer = PresentRealPlayer(actor);
        if (!realPlayer)
            return false;

        if (remember)
            RememberSharedExperience(actor, description, salience, affinityChange);

        if (!g_EnableEventChatter)
            return false;

        if (chance == 0 || urand(0, 99) >= chance)
            return false;

        std::vector<Player*> candidates = JourneyBots(actor);
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [](Player* bot)
        {
            return g_DisableRepliesInCombat && bot->IsInCombat();
        }), candidates.end());
        if (candidates.empty())
            return false;

        std::shuffle(candidates.begin(), candidates.end(), RandomEngine::Instance());
        Player* bot = candidates.front();

        if (!gesture.empty() && bot->GetMapId() == realPlayer->GetMapId()
            && bot->GetDistance(realPlayer) <= g_SayDistance)
        {
            if (uint32_t const emote = ResolveEmote(bot->GetGUID().GetRawValue(), gesture))
                SubmitBotEmote(bot->GetGUID().GetRawValue(), realPlayer->GetGUID().GetRawValue(), emote);
        }

        TurnRequest request;
        request.bot = bot;
        request.other = realPlayer;
        request.kind = TurnKind::Event;
        request.key = MakeScope(ChatScope::Party, realPlayer);
        request.trigger = description;
        return RequestBotTurn(request, /*priority=*/false);
    }

    Player* BestKnownBot(Player* player, Player* preferred = nullptr)
    {
        Group* group = player ? player->GetGroup() : nullptr;
        if (!group)
            return nullptr;

        Player* best = nullptr;
        float bestAffinity = -2.0f;
        time_t const now = time(nullptr);
        for (Player* bot : GroupBots(group))
        {
            if (preferred && bot != preferred)
                continue;

            Relationship const relationship = GetRelationship(
                bot->GetGUID().GetRawValue(), player->GetGUID().GetRawValue());
            if (relationship.interactionCount == 0 || relationship.lastInteractionAt == 0)
                continue;
            if (now - static_cast<time_t>(relationship.lastInteractionAt)
                < static_cast<time_t>(g_ReunionMinAbsenceSec))
            {
                continue;
            }
            if (!best || relationship.affinity > bestAffinity)
            {
                best = bot;
                bestAffinity = relationship.affinity;
            }
        }

        return best;
    }

    void TryReunion(Player* player, Player* preferred = nullptr)
    {
        if (!g_Enable || !g_WorldLifeEnable || !player || IsBot(player) || !player->GetGroup())
            return;

        Player* bot = BestKnownBot(player, preferred);
        if (!bot)
            return;

        time_t const now = time(nullptr);
        time_t& last = g_LastReunionAt[bot->GetGUID().GetRawValue()][player->GetGUID().GetRawValue()];
        if (last != 0 && now - last < static_cast<time_t>(g_ReunionMinAbsenceSec))
            return;
        if (g_ReunionChance == 0 || urand(0, 99) >= g_ReunionChance)
            return;
        last = now;

        if (bot->GetMapId() == player->GetMapId() && bot->GetDistance(player) <= g_SayDistance)
        {
            if (uint32_t const emote = ResolveEmote(bot->GetGUID().GetRawValue(), "wave"))
                SubmitBotEmote(bot->GetGUID().GetRawValue(), player->GetGUID().GetRawValue(), emote);
        }

        TurnRequest request;
        request.bot = bot;
        request.other = player;
        request.kind = TurnKind::Event;
        request.key = MakeScope(ChatScope::Party, player);
        request.trigger = SafeFormat(
            "{} has joined you again after a long absence. You recognize them; greet them naturally, and "
            "refer to a shared memory only if one fits.", player->GetName());
        request.replyRequired = true;
        RequestBotTurn(request, /*priority=*/false);
    }

    void TryFarewell(Group* group, Player* departing)
    {
        if (!g_Enable || !g_WorldLifeEnable || !group || !departing
            || g_ReunionChance == 0 || urand(0, 99) >= g_ReunionChance)
        {
            return;
        }

        Player* realPlayer = IsBot(departing) ? FirstRealPlayer(group) : departing;
        if (!realPlayer)
            return;

        Player* bot = nullptr;
        float bestAffinity = -2.0f;
        for (Player* candidate : GroupBots(group))
        {
            if (candidate == departing)
                continue;
            Relationship const relationship = GetRelationship(
                candidate->GetGUID().GetRawValue(), realPlayer->GetGUID().GetRawValue());
            if (relationship.interactionCount == 0)
                continue;
            if (!bot || relationship.affinity > bestAffinity)
            {
                bot = candidate;
                bestAffinity = relationship.affinity;
            }
        }

        if (IsBot(departing))
        {
            Relationship const relationship = GetRelationship(
                departing->GetGUID().GetRawValue(), realPlayer->GetGUID().GetRawValue());
            if (relationship.interactionCount > 0)
                bot = departing;
        }

        if (!bot || (g_DisableRepliesInCombat && bot->IsInCombat()))
            return;

        if (bot->GetMapId() == realPlayer->GetMapId()
            && bot->GetDistance(realPlayer) <= g_SayDistance)
        {
            if (uint32_t const emote = ResolveEmote(bot->GetGUID().GetRawValue(), "wave"))
                SubmitBotEmote(bot->GetGUID().GetRawValue(), realPlayer->GetGUID().GetRawValue(), emote);
        }

        TurnRequest request;
        request.bot = bot;
        request.other = realPlayer;
        request.kind = TurnKind::Event;
        request.key = MakeScope(ChatScope::Whisper, bot, realPlayer);
        request.trigger = IsBot(departing)
            ? SafeFormat("You are leaving {}'s group. You know them; say one brief, natural goodbye.",
                         realPlayer->GetName())
            : SafeFormat("{} is leaving your group. You know them; say one brief, natural goodbye.",
                         departing->GetName());
        request.replyRequired = true;
        RequestBotTurn(request, /*priority=*/false);
    }
}

void BotMindsEvents::Dispatch(Player* actor, std::string const& description, uint32_t chance, uint32 guildId)
{
    if (!g_Enable || !g_EnableEventChatter || !actor || description.empty())
        return;

    // One roll for the event itself, so a busy moment does not become a chorus.
    if (chance == 0 || urand(0, 99) >= chance)
        return;

    const bool guildEvent = guildId != 0;

    if (guildEvent && (!g_EnableGuildChatter || !RealPlayerInGuild(guildId)))
        return;

    Player* localAudience = guildEvent ? nullptr : LocalAudience(actor);
    if (!guildEvent && !localAudience)
        return;

    std::vector<Player*> candidates;
    for (auto const& pair : ObjectAccessor::GetPlayers())
    {
        Player* bot = pair.second;
        if (!bot || bot == actor || !IsBot(bot) || !bot->IsInWorld() || bot->IsBeingTeleported())
            continue;
        if (g_DisableRepliesInCombat && bot->IsInCombat())
            continue;

        if (guildEvent)
        {
            if (bot->GetGuildId() != guildId)
                continue;
        }
        else
        {
            if (bot->GetMapId() != actor->GetMapId() || bot->GetDistance(actor) > g_EventDistance)
                continue;
        }

        candidates.push_back(bot);
    }

    if (candidates.empty())
        return;

    std::shuffle(candidates.begin(), candidates.end(), RandomEngine::Instance());

    uint32_t spoke = 0;
    for (Player* bot : candidates)
    {
        if (spoke >= g_EventMaxBots)
            break;

        // Guild events go to guild chat, group-mates talk in party, everyone else
        // reacts out loud where the event happened.
        ChatScope scope = ChatScope::Say;
        if (guildEvent)
            scope = ChatScope::Guild;
        else if (bot->GetGroup() && bot->GetGroup() == actor->GetGroup())
            scope = ChatScope::Party;

        TurnRequest request;
        request.bot     = bot;
        request.other   = actor;
        request.kind    = TurnKind::Event;
        request.key     = MakeScope(scope, scope == ChatScope::Say ? localAudience : actor);
        request.trigger = description;

        if (RequestBotTurn(request, /*priority=*/false))
            ++spoke;
    }

    if (g_DebugEnabled && spoke > 0)
        LOG_INFO("server.loading", "[BotMinds] Event '{}' picked up by {} bot(s).", description, spoke);
}

// === Script hooks ===

ChatOnKill::ChatOnKill() : PlayerScript("ChatOnKill") {}

void ChatOnKill::OnPlayerCreatureKill(Player* killer, Creature* victim)
{
    if (!killer || !victim)
        return;

    if (IsBot(killer))
        ApplyPersonaMood(killer, PersonaMoodEvent::CreatureKill);

    if (g_WorldLifeEnable && (victim->IsDungeonBoss() || victim->isWorldBoss())
        && PresentRealPlayer(killer) && !JourneyBots(killer).empty())
    {
        DispatchGroupEvent(
            killer, SafeFormat("your group defeated {}", victim->GetName()), g_JourneyBossChance,
            /*remember=*/true, 0.95f, g_SharedExperienceAffinity, "cheer");
        return;
    }

    BotMindsEvents::Dispatch(killer, SafeFormat("{} killed {}", killer->GetName(), victim->GetName()),
                             g_EventChanceKill);
}

void ChatOnKill::OnPlayerPVPKill(Player* killer, Player* killed)
{
    if (!killer || !killed)
        return;

    if (IsBot(killer))
        ApplyPersonaMood(killer, PersonaMoodEvent::PvPWin);

    BotMindsEvents::Dispatch(killer, SafeFormat("{} killed {} in a fight", killer->GetName(), killed->GetName()),
                             g_EventChancePvPKill);
}

void ChatOnKill::OnPlayerCreatureKilledByPet(Player* owner, Creature* victim)
{
    if (!owner || !victim)
        return;

    if (IsBot(owner))
        ApplyPersonaMood(owner, PersonaMoodEvent::CreatureKill);

    if (g_WorldLifeEnable && (victim->IsDungeonBoss() || victim->isWorldBoss())
        && PresentRealPlayer(owner) && !JourneyBots(owner).empty())
    {
        DispatchGroupEvent(
            owner, SafeFormat("your group defeated {}", victim->GetName()), g_JourneyBossChance,
            /*remember=*/true, 0.95f, g_SharedExperienceAffinity, "cheer");
        return;
    }

    BotMindsEvents::Dispatch(owner, SafeFormat("{}'s pet killed {}", owner->GetName(), victim->GetName()),
                             g_EventChanceKill);
}

ChatOnLoot::ChatOnLoot() : PlayerScript("ChatOnLoot") {}

void ChatOnLoot::OnPlayerStoreNewItem(Player* player, Item* item, uint32 /*count*/)
{
    if (!player || !item || !item->GetTemplate())
        return;

    ItemTemplate const* itemTemplate = item->GetTemplate();

    // Only gear worth talking about. Grey and white drops are noise.
    if (itemTemplate->Quality < ITEM_QUALITY_RARE)
        return;

    const bool isEpic = itemTemplate->Quality >= ITEM_QUALITY_EPIC;

    if (IsBot(player))
        ApplyPersonaMood(player, isEpic ? PersonaMoodEvent::EpicLoot : PersonaMoodEvent::RareLoot);

    BotMindsEvents::Dispatch(player, SafeFormat("{} looted {}", player->GetName(), itemTemplate->Name1),
                             g_EventChanceLoot);

    if (g_WorldLifeEnable && PresentRealPlayer(player) && !JourneyBots(player).empty())
    {
        RememberSharedExperience(
            player, SafeFormat("{} found {}", player->GetName(), itemTemplate->Name1),
            isEpic ? 0.75f : 0.55f, 0.0f);
    }

    if (player->GetGuildId() != 0
        && (itemTemplate->Class == ITEM_CLASS_WEAPON || itemTemplate->Class == ITEM_CLASS_ARMOR))
    {
        BotMindsEvents::Dispatch(player,
                                 SafeFormat("{} got {} gear: {}", player->GetName(),
                                            isEpic ? "epic" : "rare", itemTemplate->Name1),
                                 isEpic ? g_EventChanceGuildEpic : g_EventChanceGuildRare,
                                 player->GetGuildId());
    }
}

ChatOnDeath::ChatOnDeath() : PlayerScript("ChatOnDeath") {}

void ChatOnDeath::OnPlayerJustDied(Player* player)
{
    if (!player)
        return;

    if (IsBot(player))
        ApplyPersonaMood(player, PersonaMoodEvent::Death);

    if (g_WorldLifeEnable && PresentRealPlayer(player) && !JourneyBots(player).empty())
        RememberSharedExperience(player, SafeFormat("{} died", player->GetName()), 0.65f, 0.0f);

    BotMindsEvents::Dispatch(player, SafeFormat("{} died", player->GetName()), g_EventChanceDeath);
}

ChatOnQuest::ChatOnQuest() : PlayerScript("ChatOnQuest") {}

void ChatOnQuest::OnPlayerCompleteQuest(Player* player, Quest const* quest)
{
    if (!player || !quest)
        return;

    if (IsBot(player))
        ApplyPersonaMood(player, PersonaMoodEvent::QuestComplete);

    ResolveQuestMemories(player->GetGUID().GetRawValue(), quest->GetQuestId(), quest->GetTitle());

    std::string const description = SafeFormat(
        "{} finished the quest {}", player->GetName(), quest->GetTitle());
    if (g_WorldLifeEnable && PresentRealPlayer(player) && !JourneyBots(player).empty())
    {
        DispatchGroupEvent(player, description, g_EventChanceQuest,
                           /*remember=*/true, 0.8f, g_SharedExperienceAffinity, "cheer");
    }
    else
    {
        BotMindsEvents::Dispatch(player, description, g_EventChanceQuest);
    }

    if (player->GetGuildId() != 0 && player->GetMap() && player->GetMap()->IsDungeon())
    {
        BotMindsEvents::Dispatch(
            player,
            SafeFormat("{} completed {} in {}", player->GetName(), quest->GetTitle(),
                       player->GetMap()->GetMapName()),
            g_EventChanceDungeonComplete, player->GetGuildId());
    }
}

ChatOnLearn::ChatOnLearn() : PlayerScript("ChatOnLearn") {}

void ChatOnLearn::OnPlayerLearnSpell(Player* player, uint32 spellID)
{
    if (!player)
        return;

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellID);
    if (!spellInfo || !spellInfo->SpellName[0] || !*spellInfo->SpellName[0])
        return;

    if (IsBot(player))
        ApplyPersonaMood(player, PersonaMoodEvent::LearnedSpell);

    BotMindsEvents::Dispatch(player, SafeFormat("{} learned {}", player->GetName(), spellInfo->SpellName[0]),
                             g_EventChanceSpell);
}

ChatOnDuel::ChatOnDuel() : PlayerScript("ChatOnDuel") {}

void ChatOnDuel::OnPlayerDuelRequest(Player* target, Player* challenger)
{
    if (!target || !challenger)
        return;

    BotMindsEvents::Dispatch(
        challenger, SafeFormat("{} challenged {} to a duel", challenger->GetName(), target->GetName()),
        g_EventChanceDuel);
}

void ChatOnDuel::OnPlayerDuelStart(Player* player1, Player* player2)
{
    if (!player1 || !player2)
        return;

    BotMindsEvents::Dispatch(player1, SafeFormat("{} and {} started duelling", player1->GetName(), player2->GetName()),
                             g_EventChanceDuel);
}

void ChatOnDuel::OnPlayerDuelEnd(Player* winner, Player* loser, DuelCompleteType /*type*/)
{
    if (!winner || !loser)
        return;

    if (IsBot(winner))
        ApplyPersonaMood(winner, PersonaMoodEvent::DuelWin);
    if (IsBot(loser))
        ApplyPersonaMood(loser, PersonaMoodEvent::DuelLoss);

    BotMindsEvents::Dispatch(winner, SafeFormat("{} beat {} in a duel", winner->GetName(), loser->GetName()),
                             g_EventChanceDuel);
}

ChatOnLevelUp::ChatOnLevelUp() : PlayerScript("ChatOnLevelUp") {}

void ChatOnLevelUp::OnPlayerLevelChanged(Player* player, uint8 /*oldLevel*/)
{
    if (!player)
        return;

    if (IsBot(player))
        ApplyPersonaMood(player, PersonaMoodEvent::LevelUp);

    std::string description = SafeFormat("{} reached level {}", player->GetName(), player->GetLevel());

    if (player->GetGroup() && FirstRealPlayer(player->GetGroup()))
        RememberSharedExperience(player, description, 0.8f, 0.0f);

    BotMindsEvents::Dispatch(player, description, g_EventChanceLevelUp);

    BotMindsEvents::Dispatch(player, description, g_EventChanceGuildLevelUp, player->GetGuildId());
}

ChatOnAchievement::ChatOnAchievement() : PlayerScript("ChatOnAchievement") {}

void ChatOnAchievement::OnPlayerAchievementComplete(Player* player, AchievementEntry const* achievement)
{
    if (!player || !achievement || !achievement->name[0] || !*achievement->name[0])
        return;

    if (IsBot(player))
        ApplyPersonaMood(player, PersonaMoodEvent::Achievement);

    std::string const description = SafeFormat(
        "{} earned the achievement {}", player->GetName(), achievement->name[0]);
    BotMindsEvents::Dispatch(player, description, g_EventChanceAchievement);
    if (!IsBot(player))
        BotMindsEvents::Dispatch(player, description, g_EventChanceGuildAchievement, player->GetGuildId());
}

ChatOnGameObjectUse::ChatOnGameObjectUse() : AllGameObjectScript("ChatOnGameObjectUse") {}

bool ChatOnGameObjectUse::CanGameObjectGossipHello(Player* player, GameObject* gameObject)
{
    if (player && gameObject && gameObject->GetGOInfo())
    {
        BotMindsEvents::Dispatch(
            player, SafeFormat("{} used {}", player->GetName(), gameObject->GetGOInfo()->name),
            g_EventChanceObjectUse);
    }

    // This observer did not handle the interaction; normal processing continues.
    return false;
}

ChatOnGuildChange::ChatOnGuildChange() : GuildScript("ChatOnGuildChange") {}

void ChatOnGuildChange::OnAddMember(Guild* guild, Player* player, uint8& /*plRank*/)
{
    if (!guild || !player)
        return;

    BotMindsEvents::Dispatch(player, SafeFormat("{} joined the guild", player->GetName()),
                             g_EventChanceGuildMember, guild->GetId());
}

void ChatOnGuildChange::OnRemoveMember(Guild* guild, Player* player, bool /*isDisbanding*/, bool isKicked)
{
    if (!guild || !player)
        return;

    BotMindsEvents::Dispatch(player,
                             SafeFormat("{} {} the guild", player->GetName(), isKicked ? "was kicked from" : "left"),
                             g_EventChanceGuildMember, guild->GetId());
}

void ChatOnGuildChange::OnEvent(Guild* guild, uint8 eventType, ObjectGuid::LowType /*playerGuid1*/,
                                ObjectGuid::LowType playerGuid2, uint8 /*newRank*/)
{
    if (!guild || (eventType != GUILD_EVENT_LOG_PROMOTE_PLAYER
        && eventType != GUILD_EVENT_LOG_DEMOTE_PLAYER))
    {
        return;
    }

    Player* player = ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(playerGuid2));
    if (!player)
        return;

    bool const promoted = eventType == GUILD_EVENT_LOG_PROMOTE_PLAYER;
    BotMindsEvents::Dispatch(
        player, SafeFormat("{} was {} in the guild", player->GetName(), promoted ? "promoted" : "demoted"),
        promoted ? g_EventChanceGuildPromotion : g_EventChanceGuildDemotion, guild->GetId());
}

ChatOnGuildLogin::ChatOnGuildLogin() : PlayerScript("ChatOnGuildLogin") {}

void ChatOnGuildLogin::OnPlayerLogin(Player* player)
{
    if (!player || IsBot(player) || player->GetGuildId() == 0)
        return;

    BotMindsEvents::Dispatch(
        player, SafeFormat("{} logged in", player->GetName()),
        g_EventChanceGuildLogin, player->GetGuildId());
}

ChatOnJourney::ChatOnJourney() : PlayerScript("ChatOnJourney") {}

void ChatOnJourney::OnPlayerLogin(Player* player)
{
    if (!player)
        return;

    uint64_t const guid = player->GetGUID().GetRawValue();
    g_LastKnownZone[guid] = player->GetZoneId();
    g_LastKnownMap[guid] = player->GetMapId();
    if (!IsBot(player))
        TryReunion(player);
}

void ChatOnJourney::OnPlayerBeforeLogout(Player* player)
{
    if (!player)
        return;

    if (!IsBot(player))
        TryFarewell(player->GetGroup(), player);

    uint64_t const guid = player->GetGUID().GetRawValue();
    g_LastKnownZone.erase(guid);
    g_LastKnownMap.erase(guid);
}

void ChatOnJourney::OnPlayerUpdateZone(Player* player, uint32 newZone, uint32 /*newArea*/)
{
    if (!player)
        return;

    uint64_t const guid = player->GetGUID().GetRawValue();
    auto result = g_LastKnownZone.emplace(guid, newZone);
    if (result.second)
        return;
    if (result.first->second == newZone)
        return;
    result.first->second = newZone;

    if (!PresentRealPlayer(player) || JourneyBots(player).empty() || !JourneyReady(player))
        return;

    DispatchGroupEvent(
        player, SafeFormat("your group has entered {}", AreaName(newZone)), g_JourneyZoneChance,
        /*remember=*/false, 0.0f, 0.0f);
}

void ChatOnJourney::OnPlayerUpdateArea(Player* player, uint32 oldArea, uint32 newArea)
{
    if (!player || !PresentRealPlayer(player) || JourneyBots(player).empty())
        return;
    if (IsTown(oldArea) || !IsTown(newArea) || !JourneyReady(player))
        return;

    DispatchGroupEvent(
        player, SafeFormat("your group has reached {} and can rest, repair or sell", AreaName(newArea)),
        g_JourneyTownChance, /*remember=*/false, 0.0f, 0.0f, "sigh");
}

void ChatOnJourney::OnPlayerMapChanged(Player* player)
{
    if (!player)
        return;

    uint64_t const guid = player->GetGUID().GetRawValue();
    auto result = g_LastKnownMap.emplace(guid, player->GetMapId());
    if (result.second)
        return;
    if (result.first->second == player->GetMapId())
        return;
    result.first->second = player->GetMapId();

    if (!player->GetMap() || !player->GetMap()->IsDungeon()
        || !PresentRealPlayer(player) || JourneyBots(player).empty() || !JourneyReady(player))
    {
        return;
    }

    DispatchGroupEvent(
        player, SafeFormat("your group has entered {}", player->GetMap()->GetMapName()),
        g_JourneyDungeonChance, /*remember=*/true, 0.7f,
        g_SharedExperienceAffinity * 0.5f, "salute");
}

ChatOnGroupLife::ChatOnGroupLife() : GroupScript("ChatOnGroupLife") {}

void ChatOnGroupLife::OnAddMember(Group* group, ObjectGuid guid)
{
    if (!group)
        return;

    Player* member = ObjectAccessor::FindConnectedPlayer(guid);
    if (!member)
        return;

    if (!IsBot(member))
    {
        TryReunion(member);
        return;
    }

    if (Player* realPlayer = FirstRealPlayer(group))
        TryReunion(realPlayer, member);
}

void ChatOnGroupLife::OnRemoveMember(Group* group, ObjectGuid guid, RemoveMethod /*method*/,
                                     ObjectGuid /*kicker*/, char const* /*reason*/)
{
    if (!group)
        return;

    if (Player* departing = ObjectAccessor::FindConnectedPlayer(guid))
        TryFarewell(group, departing);
}

void ChatOnGroupLife::OnDisband(Group* group)
{
    if (group)
        g_LastJourneyAt.erase(group->GetGUID().GetRawValue());
}
