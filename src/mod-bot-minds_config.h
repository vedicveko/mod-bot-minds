#ifndef MOD_BOT_MINDS_CONFIG_H
#define MOD_BOT_MINDS_CONFIG_H

#include <cstdint>
#include <string>
#include <vector>

#include "ScriptMgr.h"

// --------------------------------------------
// Core
// --------------------------------------------
extern bool g_Enable;
extern bool g_DebugEnabled;
extern bool g_DebugShowFullPrompt;

// --------------------------------------------
// Provider
// --------------------------------------------
extern std::string g_CloudProvider;   // "anthropic" | "openai" | "ollama"
extern std::string g_CloudApiKey;     // optional for local Ollama
extern std::string g_ApiKeyEnv;       // optional environment variable containing the key
extern std::string g_CloudModel;
extern std::string g_ProviderUrl;      // used by providers with configurable endpoints
extern uint32_t    g_CloudMaxTokens;
extern uint32_t    g_CloudTimeoutSec;
extern uint32_t    g_MaxReplyChars;
extern bool        g_StripMarkdown;
extern bool        g_StripDecorativeUnicode;

// --------------------------------------------
// Routing: which channels bots listen to
// --------------------------------------------
extern uint32_t g_HandleWhispers;
extern uint32_t g_HandleSay;
extern uint32_t g_HandleParty;
extern uint32_t g_HandleGuild;
extern uint32_t g_HandleChannel;

// --------------------------------------------
// Attention: who answers, and how often
// --------------------------------------------
extern uint32_t g_ReplyChanceWhisper;
extern uint32_t g_ReplyChanceSay;
extern uint32_t g_ReplyChanceParty;
extern uint32_t g_ReplyChanceGuild;
extern uint32_t g_ReplyChanceChannel;
extern uint32_t g_ReplyChanceBotToBot;
extern uint32_t g_InterjectChance;      // chance a bystander chips in when someone else holds the floor
extern uint32_t g_FloorWindowSec;       // how long the last speaker keeps the reply
extern uint32_t g_SmallGroupSize;       // at or below this many listeners, one always answers
extern uint32_t g_MaxBotsToPick;
extern uint32_t g_MaxBotChainDepth;     // bot-to-bot hops allowed after a human's line
extern float    g_SayDistance;

// --------------------------------------------
// Transcript
// --------------------------------------------
extern uint32_t g_TranscriptLines;
extern uint32_t g_TranscriptMaxChars;
extern uint32_t g_TranscriptTtlSec;

// --------------------------------------------
// Memory
// --------------------------------------------
extern uint32_t g_RecentMemoryCount;
extern uint32_t g_RelevantMemoryCount;
extern uint32_t g_MaxMemoriesPerSubject;
extern uint32_t g_MaxMemoryPromptChars;

// --------------------------------------------
// Rate limits
// --------------------------------------------
extern float    g_ProximityRadius;
extern uint32_t g_PerBotCooldownSec;
extern uint32_t g_MaxConcurrentCalls;
extern uint32_t g_DispatchWorkerThreads;
extern uint32_t g_MaxQueueDepth;
extern uint32_t g_MaxCallsPerMinute;
extern uint32_t g_PerScopeCooldownSec;
extern uint32_t g_MaxCallsPerScopePerMinute;
extern uint32_t g_BotHistorySize;
extern uint32_t g_ScopeHistorySize;
extern float    g_RepetitionSimilarityThreshold;
extern uint32_t g_RepetitionWindowSec;
extern uint32_t g_OpenerHistorySize;
extern bool     g_DisableRepliesInCombat;

// --------------------------------------------
// Ambient chatter
// --------------------------------------------
extern bool     g_EnableAmbientChatter;
extern uint32_t g_AmbientChance;
extern uint32_t g_AmbientMinIntervalSec;
extern uint32_t g_AmbientMaxIntervalSec;
extern bool     g_AmbientUseGeneralChannel;
extern bool     g_AmbientUseTradeChannel;
extern bool     g_AmbientUseLfgChannel;
extern bool     g_AmbientUseGuildRecruitmentChannel;

// --------------------------------------------
// Event chatter
// --------------------------------------------
extern bool     g_EnableEventChatter;
extern bool     g_EnableGuildChatter;
extern float    g_EventDistance;
extern uint32_t g_EventMaxBots;
extern uint32_t g_EventChanceKill;
extern uint32_t g_EventChancePvPKill;
extern uint32_t g_EventChanceLoot;
extern uint32_t g_EventChanceDeath;
extern uint32_t g_EventChanceQuest;
extern uint32_t g_EventChanceSpell;
extern uint32_t g_EventChanceDuel;
extern uint32_t g_EventChanceLevelUp;
extern uint32_t g_EventChanceAchievement;
extern uint32_t g_EventChanceObjectUse;
extern uint32_t g_EventChanceGuildEpic;
extern uint32_t g_EventChanceGuildRare;
extern uint32_t g_EventChanceGuildLevelUp;
extern uint32_t g_EventChanceGuildMember;
extern uint32_t g_EventChanceGuildLogin;
extern uint32_t g_EventChanceGuildPromotion;
extern uint32_t g_EventChanceGuildDemotion;
extern uint32_t g_EventChanceGuildAchievement;
extern uint32_t g_EventChanceDungeonComplete;

// --------------------------------------------
// World life: shared journeys, reunions and quiet physical reactions
// --------------------------------------------
extern bool     g_WorldLifeEnable;
extern uint32_t g_JourneyZoneChance;
extern uint32_t g_JourneyTownChance;
extern uint32_t g_JourneyDungeonChance;
extern uint32_t g_JourneyBossChance;
extern uint32_t g_JourneyCooldownSec;
extern uint32_t g_ReunionChance;
extern uint32_t g_ReunionMinAbsenceSec;
extern float    g_SharedExperienceAffinity;
extern uint32_t g_IdleGestureChance;

// --------------------------------------------
// Reactions to player text emotes aimed at a bot
// --------------------------------------------
extern bool     g_EnableEmoteReactions;
extern uint32_t g_EmoteReactionChance;
extern uint32_t g_EmoteReactionMirrorWeight;
extern uint32_t g_EmoteReactionCounterWeight;
extern uint32_t g_EmoteReactionSpeakWeight;
extern uint32_t g_EmoteReactionCooldownSec;

// --------------------------------------------
// Actions: what bots will actually do, not just say
// --------------------------------------------
extern bool     g_ActionsEnable;
extern uint32_t g_ActionMaxAttempts;
extern float    g_GiftMinAffinity;      // how much a bot must like you before it parts with coin
extern uint32_t g_GiftMaxCopper;        // absolute ceiling on one gift; 0 disables gold entirely
extern uint32_t g_GiftCopperPerLevel;   // scales the ceiling with the giver's level
extern uint32_t g_GiftCooldownSec;      // per bot, per person
extern uint32_t g_UnpromptedChance;     // chance an ambient turn is a favour rather than a remark
extern uint32_t g_UnpromptedCooldownSec;
extern uint32_t g_ConversationHoldSec;  // how long a bot stands still after speaking to somebody
extern uint32_t g_EmoteCooldownSec;     // least time between one bot's gestures; 0 disables them

// --------------------------------------------
// Helpful recovery: unsolicited resurrection and post-combat healing
// --------------------------------------------
extern bool     g_RecoveryEnable;
extern uint32_t g_RecoveryScanIntervalSec;
extern float    g_RecoveryDistance;
extern uint32_t g_RecoveryResurrectChance;
extern uint32_t g_RecoveryHealChance;
extern uint32_t g_RecoveryHealBelowPct;
extern uint32_t g_RecoveryCooldownSec;

// --------------------------------------------
// Reciprocity: acknowledge real players who help bots
// --------------------------------------------
extern bool     g_ReciprocityEnable;
extern float    g_ReciprocityAffinityGain;
extern uint32_t g_ReciprocityCooldownSec;
extern uint32_t g_ReciprocitySpeakChance;

// --------------------------------------------
// Presentation
// --------------------------------------------
extern bool     g_EnableTypingSimulation;
extern uint32_t g_TypingSimulationBaseDelay;
extern uint32_t g_TypingSimulationDelayPerChar;
extern uint32_t g_TypingSimulationMaxDelay;

// --------------------------------------------
// Playerbot commands typed in chat, which are instructions rather than talk.
// --------------------------------------------
extern std::vector<std::string> g_BlacklistCommands;

// --------------------------------------------
// How often persona and relationship changes are written back (minutes).
// Memories are written as they happen and are not affected by this.
// --------------------------------------------
extern uint32_t g_SaveIntervalMinutes;

void LoadBotMindsConfig();

class BotMindsConfigWorldScript : public WorldScript
{
public:
    BotMindsConfigWorldScript();
    void OnStartup() override;
    void OnShutdown() override;
    void OnUpdate(uint32 diff) override;
};

#endif // MOD_BOT_MINDS_CONFIG_H
