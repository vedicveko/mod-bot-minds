#include "mod-bot-minds_kindness.h"
#include "mod-bot-minds_action.h"
#include "mod-bot-minds_config.h"
#include "mod-bot-minds_persona.h"
#include "mod-bot-minds_relationship.h"
#include "mod-bot-minds-utilities.h"

#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "Random.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellAuraDefines.h"
#include "SpellInfo.h"
#include "Unit.h"

#include <algorithm>
#include <ctime>
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
    using PairKey = std::pair<uint64_t, uint64_t>;

    struct PairHash
    {
        std::size_t operator()(PairKey const& pair) const
        {
            return std::hash<uint64_t>()(pair.first) ^ (std::hash<uint64_t>()(pair.second) << 1);
        }
    };

    struct PendingThanks
    {
        uint64_t helperGuid = 0;
        time_t createdAt = 0;
    };

    struct PendingResurrection
    {
        uint64_t helperGuid = 0;
        time_t createdAt = 0;
    };

    std::unordered_map<uint64_t, time_t> g_LastRecoveryAt;
    std::unordered_map<PairKey, time_t, PairHash> g_LastHelpAt;
    std::unordered_map<PairKey, PendingThanks, PairHash> g_PendingThanks;
    std::unordered_map<uint64_t, PendingResurrection> g_PendingResurrections;
    std::unordered_map<uint64_t, time_t> g_LastVisibleThanksAt;
    std::unordered_map<uint64_t, uint32_t> g_NextThanksReply;

    constexpr time_t THANKS_EXPIRE_SEC = 120;
    constexpr time_t RESURRECTION_EXPIRE_SEC = 120;
    constexpr time_t VISIBLE_THANKS_COOLDOWN_SEC = 10;

    bool IsBot(Player* player)
    {
        if (!player)
            return false;
        if (player->GetSession() && player->GetSession()->IsBot())
            return true;

        PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(player);
        return botAI && botAI->IsBotAI();
    }

    bool IsHealingSpell(SpellInfo const* spellInfo)
    {
        return spellInfo && (spellInfo->HasEffect(SPELL_EFFECT_HEAL)
            || spellInfo->HasEffect(SPELL_EFFECT_HEAL_MAX_HEALTH)
            || spellInfo->HasEffect(SPELL_EFFECT_HEAL_MECHANICAL)
            || spellInfo->HasAura(SPELL_AURA_PERIODIC_HEAL));
    }

    bool IsResurrectionSpell(SpellInfo const* spellInfo)
    {
        return spellInfo && (spellInfo->HasEffect(SPELL_EFFECT_RESURRECT)
            || spellInfo->HasEffect(SPELL_EFFECT_RESURRECT_NEW));
    }

    void RecordHelp(Player* helper, Player* bot, std::string const& reason)
    {
        if (!g_Enable || !g_ReciprocityEnable || !helper || !bot || helper == bot)
            return;
        if (IsBot(helper) || !IsBot(bot))
            return;

        time_t const now = time(nullptr);
        PairKey const key(bot->GetGUID().GetRawValue(), helper->GetGUID().GetRawValue());

        auto last = g_LastHelpAt.find(key);
        if (last != g_LastHelpAt.end()
            && now < last->second + static_cast<time_t>(g_ReciprocityCooldownSec))
        {
            return;
        }

        g_LastHelpAt[key] = now;
        ApplyPersonaMood(bot, PersonaMoodEvent::ReceivedHelp);
        if (g_ReciprocityAffinityGain > 0.0f)
        {
            ApplyRelationshipDelta(key.first, key.second, /*otherIsBot=*/false,
                                   g_ReciprocityAffinityGain, reason);
        }

        g_PendingThanks[key] = { key.second, now };

        if (g_DebugEnabled)
        {
            LOG_INFO("server.loading", "[BotMinds] {} credited help from {}: {}.",
                     bot->GetName(), helper->GetName(), reason);
        }
    }

    void DeliverThanks(time_t now)
    {
        for (auto iterator = g_PendingThanks.begin(); iterator != g_PendingThanks.end();)
        {
            PairKey const key = iterator->first;
            PendingThanks const& pending = iterator->second;

            if (now > pending.createdAt + THANKS_EXPIRE_SEC)
            {
                iterator = g_PendingThanks.erase(iterator);
                continue;
            }

            Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(key.first));
            Player* helper = ObjectAccessor::FindPlayer(ObjectGuid(pending.helperGuid));
            if (!bot || !helper || !bot->IsInWorld() || !helper->IsInWorld())
            {
                iterator = g_PendingThanks.erase(iterator);
                continue;
            }

            if (!bot->IsAlive() || bot->IsInCombat())
            {
                ++iterator;
                continue;
            }

            if (bot->GetMapId() != helper->GetMapId()
                || (g_SayDistance > 0.0f && bot->GetDistance(helper) > g_SayDistance))
            {
                iterator = g_PendingThanks.erase(iterator);
                continue;
            }

            auto visible = g_LastVisibleThanksAt.find(pending.helperGuid);
            if (visible != g_LastVisibleThanksAt.end()
                && now < visible->second + VISIBLE_THANKS_COOLDOWN_SEC)
            {
                iterator = g_PendingThanks.erase(iterator);
                continue;
            }

            SubmitBotEmote(key.first, pending.helperGuid, TEXT_EMOTE_THANK);
            if (urand(0, 99) < g_ReciprocitySpeakChance)
            {
                static char const* replies[] = {
                    "ty {}", "thanks {}", "much appreciated, {}", "nice one, {}", "appreciate it, {}",
                    "you're a lifesaver, {}", "thanks for that, {}", "that helped, {}", "cheers {}"
                };

                auto next = g_NextThanksReply.emplace(
                    pending.helperGuid, static_cast<uint32_t>(pending.helperGuid % std::size(replies))).first;
                uint32_t const replyIndex = next->second;
                next->second = (replyIndex + 1) % std::size(replies);
                bot->Say(SafeFormat(replies[replyIndex], helper->GetName()), LANG_UNIVERSAL);
            }

            g_LastVisibleThanksAt[pending.helperGuid] = now;
            iterator = g_PendingThanks.erase(iterator);
        }
    }

    void TryHelpfulRecovery(time_t now)
    {
        if (!g_ActionsEnable || !g_RecoveryEnable)
            return;

        std::vector<Player*> realPlayers;
        for (auto const& pair : ObjectAccessor::GetPlayers())
        {
            Player* player = pair.second;
            if (player && player->IsInWorld() && !IsBot(player) && !player->InBattleground())
                realPlayers.push_back(player);
        }
        std::shuffle(realPlayers.begin(), realPlayers.end(), RandomEngine::Instance());

        std::unordered_set<uint64_t> usedBots;
        for (Player* target : realPlayers)
        {
            uint64_t const targetGuid = target->GetGUID().GetRawValue();
            auto last = g_LastRecoveryAt.find(targetGuid);
            if (last != g_LastRecoveryAt.end())
            {
                if (now < last->second + static_cast<time_t>(g_RecoveryCooldownSec))
                    continue;
                g_LastRecoveryAt.erase(last);
            }

            bool const resurrect = !target->IsAlive();
            if (resurrect)
            {
                if (g_RecoveryResurrectChance == 0 || target->isResurrectRequested()
                    || urand(0, 99) >= g_RecoveryResurrectChance)
                {
                    continue;
                }
            }
            else
            {
                if (g_RecoveryHealChance == 0 || g_RecoveryHealBelowPct == 0 || target->IsInCombat()
                    || target->GetHealthPct() >= static_cast<float>(g_RecoveryHealBelowPct)
                    || urand(0, 99) >= g_RecoveryHealChance)
                {
                    continue;
                }
            }

            std::vector<Player*> candidates;
            for (auto const& pair : ObjectAccessor::GetPlayers())
            {
                Player* bot = pair.second;
                if (!bot || !bot->IsInWorld() || !IsBot(bot) || bot->IsBeingTeleported())
                    continue;
                if (usedBots.count(bot->GetGUID().GetRawValue()) != 0)
                    continue;
                if (!bot->IsWithinDistInMap(target, g_RecoveryDistance) || !bot->IsFriendlyTo(target))
                    continue;
                candidates.push_back(bot);
            }
            std::shuffle(candidates.begin(), candidates.end(), RandomEngine::Instance());

            for (Player* bot : candidates)
            {
                std::string const spellName = ChooseRecoverySpell(bot, target, resurrect);
                if (spellName.empty())
                    continue;

                BotAction action;
                action.kind = resurrect ? ActionKind::Resurrect : ActionKind::Heal;
                action.botGuid = bot->GetGUID().GetRawValue();
                action.targetGuid = targetGuid;
                action.spellName = spellName;
                SubmitBotAction(action);

                usedBots.insert(action.botGuid);
                g_LastRecoveryAt[targetGuid] = now;

                if (g_DebugEnabled)
                {
                    LOG_INFO("server.loading", "[BotMinds] {} offered passerby {} {} with {}.",
                             bot->GetName(), resurrect ? "resurrection to" : "healing to",
                             target->GetName(), spellName);
                }
                break;
            }
        }
    }
}

BotMindsKindnessWorldScript::BotMindsKindnessWorldScript() : WorldScript("BotMindsKindnessWorldScript") {}

void BotMindsKindnessWorldScript::OnUpdate(uint32 diff)
{
    if (!g_Enable)
        return;

    static uint32 recoveryTimer = 0;
    static uint32 thanksTimer = 0;
    if (recoveryTimer > diff)
        recoveryTimer -= diff;
    else
    {
        recoveryTimer = g_RecoveryScanIntervalSec * IN_MILLISECONDS;
        TryHelpfulRecovery(time(nullptr));
    }

    if (thanksTimer > diff)
        thanksTimer -= diff;
    else
    {
        thanksTimer = IN_MILLISECONDS;
        DeliverThanks(time(nullptr));
    }
}

void BotMindsReciprocityPlayerScript::OnPlayerSpellCast(Player* player, Spell* spell, bool /*skipCheck*/)
{
    if (!g_Enable || !g_ReciprocityEnable || !player || !spell || IsBot(player))
        return;

    Player* bot = spell->m_targets.GetUnitTarget() ? spell->m_targets.GetUnitTarget()->ToPlayer() : nullptr;
    if (!bot || bot == player || !IsBot(bot))
        return;

    SpellInfo const* spellInfo = spell->GetSpellInfo();
    if (!spellInfo)
        return;

    if (IsResurrectionSpell(spellInfo))
    {
        if (!bot->IsAlive())
        {
            g_PendingResurrections[bot->GetGUID().GetRawValue()] = {
                player->GetGUID().GetRawValue(), time(nullptr)
            };
        }
        return;
    }

    // Actual healing is observed by UnitScript after overheal is removed.
    if (IsHealingSpell(spellInfo) || !spellInfo->IsPositive())
        return;

    RecordHelp(player, bot, SafeFormat("helped me with {}", spellInfo->SpellName[0]));
}

void BotMindsReciprocityPlayerScript::OnPlayerResurrect(Player* player, float /*restorePercent*/,
                                                         bool& /*applySickness*/)
{
    if (!g_Enable || !g_ReciprocityEnable || !player || !IsBot(player))
        return;

    uint64_t const botGuid = player->GetGUID().GetRawValue();
    auto pending = g_PendingResurrections.find(botGuid);
    if (pending == g_PendingResurrections.end())
        return;

    PendingResurrection const resurrection = pending->second;
    g_PendingResurrections.erase(pending);
    if (time(nullptr) > resurrection.createdAt + RESURRECTION_EXPIRE_SEC)
        return;

    Player* helper = ObjectAccessor::FindPlayer(ObjectGuid(resurrection.helperGuid));
    if (helper)
        RecordHelp(helper, player, "resurrected me");
}

void BotMindsReciprocityUnitScript::OnHeal(Unit* healer, Unit* receiver, uint32& gain)
{
    if (!g_Enable || !g_ReciprocityEnable || !healer || !receiver || gain == 0)
        return;

    Player* helper = healer->GetCharmerOrOwnerPlayerOrPlayerItself();
    Player* bot = receiver->ToPlayer();
    if (helper && bot)
        RecordHelp(helper, bot, "healed me");
}
