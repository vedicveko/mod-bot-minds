#include "mod-bot-minds_config.h"
#include "mod-bot-minds_action.h"
#include "mod-bot-minds_dispatch.h"
#include "mod-bot-minds_governor.h"
#include "mod-bot-minds_llmclient.h"
#include "mod-bot-minds_memory.h"
#include "mod-bot-minds_persona.h"
#include "mod-bot-minds_relationship.h"
#include "mod-bot-minds_transcript.h"
#include "mod-bot-minds-utilities.h"

#include "Config.h"
#include "Log.h"

#include <algorithm>
#include <cctype>
#include <ctime>

// --------------------------------------------
// Core
// --------------------------------------------
bool g_Enable              = true;
bool g_DebugEnabled        = false;
bool g_DebugShowFullPrompt = false;

// --------------------------------------------
// Provider
// --------------------------------------------
std::string g_CloudProvider  = "anthropic";
std::string g_CloudApiKey     = "";
std::string g_ApiKeyEnv       = "";
std::string g_CloudModel      = "claude-haiku-4-5";
std::string g_ProviderUrl     = "http://localhost:11434/api/chat";
uint32_t    g_CloudMaxTokens  = 512;
uint32_t    g_CloudTimeoutSec = 30;
uint32_t    g_MaxReplyChars   = 200;
bool        g_StripMarkdown   = true;
bool        g_StripDecorativeUnicode = true;

// --------------------------------------------
// Routing
// --------------------------------------------
uint32_t g_HandleWhispers = 1;
uint32_t g_HandleSay      = 1;
uint32_t g_HandleParty    = 1;
uint32_t g_HandleGuild    = 1;
uint32_t g_HandleChannel  = 0;

// --------------------------------------------
// Attention
// --------------------------------------------
uint32_t g_ReplyChanceWhisper  = 100;
uint32_t g_ReplyChanceSay      = 70;
uint32_t g_ReplyChanceParty    = 85;
uint32_t g_ReplyChanceGuild    = 50;
uint32_t g_ReplyChanceChannel  = 35;
uint32_t g_ReplyChanceBotToBot = 12;
uint32_t g_InterjectChance     = 12;
uint32_t g_FloorWindowSec      = 60;
uint32_t g_SmallGroupSize      = 3;
uint32_t g_MaxBotsToPick       = 2;
uint32_t g_MaxBotChainDepth    = 2;
float    g_SayDistance         = 30.0f;

// --------------------------------------------
// Transcript
// --------------------------------------------
uint32_t g_TranscriptLines    = 8;
uint32_t g_TranscriptMaxChars = 700;
uint32_t g_TranscriptTtlSec   = 900;

// --------------------------------------------
// Memory
// --------------------------------------------
uint32_t g_RecentMemoryCount     = 6;
uint32_t g_RelevantMemoryCount   = 4;
uint32_t g_MaxMemoriesPerSubject = 30;
uint32_t g_MaxMemoryPromptChars  = 1200;

// --------------------------------------------
// Rate limits
// --------------------------------------------
float    g_ProximityRadius         = 40.0f;
uint32_t g_PerBotCooldownSec       = 12;
uint32_t g_MaxConcurrentCalls      = 3;
uint32_t g_DispatchWorkerThreads   = 3;
uint32_t g_MaxQueueDepth           = 64;
uint32_t g_MaxCallsPerMinute       = 60;
uint32_t g_PerScopeCooldownSec     = 15;
uint32_t g_MaxCallsPerScopePerMinute = 8;
uint32_t g_BotHistorySize          = 12;
uint32_t g_ScopeHistorySize        = 30;
float    g_RepetitionSimilarityThreshold = 0.72f;
uint32_t g_RepetitionWindowSec     = 1800;
uint32_t g_OpenerHistorySize       = 8;
bool     g_DisableRepliesInCombat  = true;

// --------------------------------------------
// Ambient chatter
// --------------------------------------------
bool     g_EnableAmbientChatter = true;
uint32_t g_AmbientChance         = 25;
uint32_t g_AmbientMinIntervalSec = 120;
uint32_t g_AmbientMaxIntervalSec = 600;
bool     g_AmbientUseGeneralChannel = false;
bool     g_AmbientUseTradeChannel = false;
bool     g_AmbientUseLfgChannel = false;
bool     g_AmbientUseGuildRecruitmentChannel = false;

// --------------------------------------------
// Event chatter
// --------------------------------------------
bool     g_EnableEventChatter    = true;
bool     g_EnableGuildChatter    = true;
float    g_EventDistance         = 40.0f;
uint32_t g_EventMaxBots          = 1;
uint32_t g_EventChanceKill       = 1;
uint32_t g_EventChancePvPKill    = 15;
uint32_t g_EventChanceLoot       = 15;
uint32_t g_EventChanceDeath      = 25;
uint32_t g_EventChanceQuest      = 20;
uint32_t g_EventChanceSpell      = 2;
uint32_t g_EventChanceDuel       = 25;
uint32_t g_EventChanceLevelUp    = 60;
uint32_t g_EventChanceAchievement = 75;
uint32_t g_EventChanceObjectUse  = 10;
uint32_t g_EventChanceGuildEpic  = 80;
uint32_t g_EventChanceGuildRare  = 30;
uint32_t g_EventChanceGuildLevelUp = 50;
uint32_t g_EventChanceGuildMember  = 60;
uint32_t g_EventChanceGuildLogin = 60;
uint32_t g_EventChanceGuildPromotion = 25;
uint32_t g_EventChanceGuildDemotion = 5;
uint32_t g_EventChanceGuildAchievement = 75;
uint32_t g_EventChanceDungeonComplete = 85;

// --------------------------------------------
// World life
// --------------------------------------------
bool     g_WorldLifeEnable          = true;
uint32_t g_JourneyZoneChance        = 35;
uint32_t g_JourneyTownChance        = 45;
uint32_t g_JourneyDungeonChance     = 75;
uint32_t g_JourneyBossChance        = 90;
uint32_t g_JourneyCooldownSec       = 120;
uint32_t g_ReunionChance            = 70;
uint32_t g_ReunionMinAbsenceSec     = 21600;
float    g_SharedExperienceAffinity = 0.01f;
uint32_t g_IdleGestureChance        = 20;

// --------------------------------------------
// Player emote reactions
// --------------------------------------------
bool     g_EnableEmoteReactions        = true;
uint32_t g_EmoteReactionChance         = 60;
uint32_t g_EmoteReactionMirrorWeight   = 55;
uint32_t g_EmoteReactionCounterWeight  = 30;
uint32_t g_EmoteReactionSpeakWeight    = 15;
uint32_t g_EmoteReactionCooldownSec    = 20;

// --------------------------------------------
// Actions
// --------------------------------------------
bool     g_ActionsEnable         = true;
uint32_t g_ActionMaxAttempts     = 3;
float    g_GiftMinAffinity       = 0.25f;
uint32_t g_GiftMaxCopper         = 5000;
uint32_t g_GiftCopperPerLevel    = 100;
uint32_t g_GiftCooldownSec       = 86400;
uint32_t g_UnpromptedChance      = 20;
uint32_t g_UnpromptedCooldownSec = 900;
uint32_t g_ConversationHoldSec   = 8;
uint32_t g_EmoteCooldownSec      = 180;

// --------------------------------------------
// Helpful recovery
// --------------------------------------------
bool     g_RecoveryEnable          = true;
uint32_t g_RecoveryScanIntervalSec = 10;
float    g_RecoveryDistance        = 30.0f;
uint32_t g_RecoveryResurrectChance = 75;
uint32_t g_RecoveryHealChance      = 35;
uint32_t g_RecoveryHealBelowPct    = 55;
uint32_t g_RecoveryCooldownSec     = 300;

// --------------------------------------------
// Reciprocity
// --------------------------------------------
bool     g_ReciprocityEnable      = true;
float    g_ReciprocityAffinityGain = 0.02f;
uint32_t g_ReciprocityCooldownSec = 300;
uint32_t g_ReciprocitySpeakChance = 35;

// --------------------------------------------
// Presentation
// --------------------------------------------
bool     g_EnableTypingSimulation       = false;
uint32_t g_TypingSimulationBaseDelay    = 1000;
uint32_t g_TypingSimulationDelayPerChar = 25;
uint32_t g_TypingSimulationMaxDelay     = 8000;

std::vector<std::string> g_BlacklistCommands;

uint32_t g_SaveIntervalMinutes = 10;

static time_t g_LastSaveTime = 0;

void LoadBotMindsConfig()
{
    g_Enable              = sConfigMgr->GetOption<bool>("BotMinds.Enable", true);
    g_DebugEnabled        = sConfigMgr->GetOption<bool>("BotMinds.DebugEnabled", false);
    g_DebugShowFullPrompt = sConfigMgr->GetOption<bool>("BotMinds.DebugShowFullPrompt", false);

    g_CloudProvider  = sConfigMgr->GetOption<std::string>("BotMinds.Provider", "anthropic");
    std::transform(g_CloudProvider.begin(), g_CloudProvider.end(), g_CloudProvider.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    g_CloudApiKey    = sConfigMgr->GetOption<std::string>("BotMinds.ApiKey", "");
    g_ApiKeyEnv      = sConfigMgr->GetOption<std::string>("BotMinds.ApiKeyEnv", "");
    g_CloudModel     = sConfigMgr->GetOption<std::string>("BotMinds.Model", "claude-haiku-4-5");
    g_ProviderUrl    = sConfigMgr->GetOption<std::string>(
        "BotMinds.Url", "http://localhost:11434/api/chat");
    g_CloudMaxTokens = sConfigMgr->GetOption<uint32_t>("BotMinds.MaxTokens", 512);
    g_CloudTimeoutSec = sConfigMgr->GetOption<uint32_t>("BotMinds.TimeoutSec", 30);
    g_MaxReplyChars  = sConfigMgr->GetOption<uint32_t>("BotMinds.MaxReplyChars", 200);
    g_StripMarkdown = sConfigMgr->GetOption<bool>("BotMinds.Response.StripMarkdown", true);
    g_StripDecorativeUnicode = sConfigMgr->GetOption<bool>(
        "BotMinds.Response.StripDecorativeUnicode", true);

    g_HandleWhispers = sConfigMgr->GetOption<uint32_t>("BotMinds.Route.HandleWhispers", 1);
    g_HandleSay      = sConfigMgr->GetOption<uint32_t>("BotMinds.Route.HandleSay", 1);
    g_HandleParty    = sConfigMgr->GetOption<uint32_t>("BotMinds.Route.HandleParty", 1);
    g_HandleGuild    = sConfigMgr->GetOption<uint32_t>("BotMinds.Route.HandleGuild", 1);
    g_HandleChannel  = sConfigMgr->GetOption<uint32_t>("BotMinds.Route.HandleChannel", 0);

    g_ReplyChanceWhisper  = sConfigMgr->GetOption<uint32_t>("BotMinds.ReplyChance.Whisper", 100);
    g_ReplyChanceSay      = sConfigMgr->GetOption<uint32_t>("BotMinds.ReplyChance.Say", 70);
    g_ReplyChanceParty    = sConfigMgr->GetOption<uint32_t>("BotMinds.ReplyChance.Party", 85);
    g_ReplyChanceGuild    = sConfigMgr->GetOption<uint32_t>("BotMinds.ReplyChance.Guild", 50);
    g_ReplyChanceChannel  = sConfigMgr->GetOption<uint32_t>("BotMinds.ReplyChance.Channel", 35);
    g_ReplyChanceBotToBot = sConfigMgr->GetOption<uint32_t>("BotMinds.ReplyChance.BotToBot", 12);
    g_InterjectChance     = sConfigMgr->GetOption<uint32_t>("BotMinds.Attention.InterjectChance", 12);
    g_FloorWindowSec      = sConfigMgr->GetOption<uint32_t>("BotMinds.Attention.FloorWindowSec", 60);
    g_SmallGroupSize      = sConfigMgr->GetOption<uint32_t>("BotMinds.Attention.SmallGroupSize", 3);
    g_MaxBotsToPick       = sConfigMgr->GetOption<uint32_t>("BotMinds.Attention.MaxBotsToPick", 2);
    g_MaxBotChainDepth    = sConfigMgr->GetOption<uint32_t>("BotMinds.Attention.MaxBotChainDepth", 2);
    g_SayDistance         = sConfigMgr->GetOption<float>("BotMinds.SayDistance", 30.0f);

    g_TranscriptLines    = sConfigMgr->GetOption<uint32_t>("BotMinds.Transcript.Lines", 8);
    g_TranscriptMaxChars = sConfigMgr->GetOption<uint32_t>("BotMinds.Transcript.MaxChars", 700);
    g_TranscriptTtlSec   = sConfigMgr->GetOption<uint32_t>("BotMinds.Transcript.TtlSec", 900);

    g_RecentMemoryCount     = sConfigMgr->GetOption<uint32_t>("BotMinds.Memory.RecentCount", 6);
    g_RelevantMemoryCount   = sConfigMgr->GetOption<uint32_t>("BotMinds.Memory.RelevantCount", 4);
    g_MaxMemoriesPerSubject = sConfigMgr->GetOption<uint32_t>("BotMinds.Memory.MaxPerSubject", 30);
    g_MaxMemoryPromptChars  = sConfigMgr->GetOption<uint32_t>("BotMinds.Memory.MaxPromptChars", 1200);

    g_ProximityRadius         = sConfigMgr->GetOption<float>("BotMinds.Limits.ProximityRadius", 40.0f);
    g_PerBotCooldownSec       = sConfigMgr->GetOption<uint32_t>("BotMinds.Limits.PerBotCooldownSec", 12);
    g_MaxConcurrentCalls      = sConfigMgr->GetOption<uint32_t>("BotMinds.Limits.MaxConcurrentCalls", 3);
    g_DispatchWorkerThreads   = sConfigMgr->GetOption<uint32_t>("BotMinds.WorkerThreads", 3);
    g_MaxQueueDepth           = sConfigMgr->GetOption<uint32_t>("BotMinds.MaxQueueDepth", 64);
    g_MaxCallsPerMinute       = sConfigMgr->GetOption<uint32_t>("BotMinds.Limits.MaxCallsPerMinute", 60);
    g_PerScopeCooldownSec     = sConfigMgr->GetOption<uint32_t>("BotMinds.Limits.PerScopeCooldownSec", 15);
    g_MaxCallsPerScopePerMinute = sConfigMgr->GetOption<uint32_t>(
        "BotMinds.Limits.MaxCallsPerScopePerMinute", 8);
    g_BotHistorySize = sConfigMgr->GetOption<uint32_t>("BotMinds.Repetition.BotHistorySize", 12);
    g_ScopeHistorySize = sConfigMgr->GetOption<uint32_t>("BotMinds.Repetition.ScopeHistorySize", 30);
    g_RepetitionSimilarityThreshold = sConfigMgr->GetOption<float>(
        "BotMinds.Repetition.SimilarityThreshold", 0.72f);
    g_RepetitionWindowSec = sConfigMgr->GetOption<uint32_t>("BotMinds.Repetition.WindowSec", 1800);
    g_OpenerHistorySize = sConfigMgr->GetOption<uint32_t>("BotMinds.Repetition.OpenerHistorySize", 8);
    g_DisableRepliesInCombat  = sConfigMgr->GetOption<bool>("BotMinds.Limits.DisableRepliesInCombat", true);

    g_EnableAmbientChatter  = sConfigMgr->GetOption<bool>("BotMinds.Ambient.Enable", true);
    g_AmbientChance         = sConfigMgr->GetOption<uint32_t>("BotMinds.Ambient.Chance", 25);
    g_AmbientMinIntervalSec = sConfigMgr->GetOption<uint32_t>("BotMinds.Ambient.MinIntervalSec", 120);
    g_AmbientMaxIntervalSec = sConfigMgr->GetOption<uint32_t>("BotMinds.Ambient.MaxIntervalSec", 600);
    g_AmbientUseGeneralChannel = sConfigMgr->GetOption<bool>("BotMinds.Ambient.Channel.General", false);
    g_AmbientUseTradeChannel = sConfigMgr->GetOption<bool>("BotMinds.Ambient.Channel.Trade", false);
    g_AmbientUseLfgChannel = sConfigMgr->GetOption<bool>("BotMinds.Ambient.Channel.LookingForGroup", false);
    g_AmbientUseGuildRecruitmentChannel = sConfigMgr->GetOption<bool>(
        "BotMinds.Ambient.Channel.GuildRecruitment", false);

    g_EnableEventChatter       = sConfigMgr->GetOption<bool>("BotMinds.Events.Enable", true);
    g_EnableGuildChatter       = sConfigMgr->GetOption<bool>("BotMinds.Events.EnableGuild", true);
    g_EventDistance            = sConfigMgr->GetOption<float>("BotMinds.Events.Distance", 40.0f);
    g_EventMaxBots             = sConfigMgr->GetOption<uint32_t>("BotMinds.Events.MaxBots", 1);
    g_EventChanceKill          = sConfigMgr->GetOption<uint32_t>("BotMinds.Events.Chance.Kill", 1);
    g_EventChancePvPKill       = sConfigMgr->GetOption<uint32_t>("BotMinds.Events.Chance.PvPKill", 15);
    g_EventChanceLoot          = sConfigMgr->GetOption<uint32_t>("BotMinds.Events.Chance.Loot", 15);
    g_EventChanceDeath         = sConfigMgr->GetOption<uint32_t>("BotMinds.Events.Chance.Death", 25);
    g_EventChanceQuest         = sConfigMgr->GetOption<uint32_t>("BotMinds.Events.Chance.Quest", 20);
    g_EventChanceSpell         = sConfigMgr->GetOption<uint32_t>("BotMinds.Events.Chance.Spell", 2);
    g_EventChanceDuel          = sConfigMgr->GetOption<uint32_t>("BotMinds.Events.Chance.Duel", 25);
    g_EventChanceLevelUp       = sConfigMgr->GetOption<uint32_t>("BotMinds.Events.Chance.LevelUp", 60);
    g_EventChanceAchievement   = sConfigMgr->GetOption<uint32_t>("BotMinds.Events.Chance.Achievement", 75);
    g_EventChanceObjectUse     = sConfigMgr->GetOption<uint32_t>("BotMinds.Events.Chance.ObjectUse", 10);
    g_EventChanceGuildEpic     = sConfigMgr->GetOption<uint32_t>("BotMinds.Events.Chance.GuildEpicGear", 80);
    g_EventChanceGuildRare     = sConfigMgr->GetOption<uint32_t>("BotMinds.Events.Chance.GuildRareGear", 30);
    g_EventChanceGuildLevelUp  = sConfigMgr->GetOption<uint32_t>("BotMinds.Events.Chance.GuildLevelUp", 50);
    g_EventChanceGuildMember   = sConfigMgr->GetOption<uint32_t>("BotMinds.Events.Chance.GuildMember", 60);
    g_EventChanceGuildLogin = sConfigMgr->GetOption<uint32_t>("BotMinds.Events.Chance.GuildLogin", 60);
    g_EventChanceGuildPromotion = sConfigMgr->GetOption<uint32_t>("BotMinds.Events.Chance.GuildPromotion", 25);
    g_EventChanceGuildDemotion = sConfigMgr->GetOption<uint32_t>("BotMinds.Events.Chance.GuildDemotion", 5);
    g_EventChanceGuildAchievement = sConfigMgr->GetOption<uint32_t>(
        "BotMinds.Events.Chance.GuildAchievement", 75);
    g_EventChanceDungeonComplete = sConfigMgr->GetOption<uint32_t>(
        "BotMinds.Events.Chance.DungeonComplete", 85);

    g_WorldLifeEnable = sConfigMgr->GetOption<bool>("BotMinds.WorldLife.Enable", true);
    g_JourneyZoneChance = sConfigMgr->GetOption<uint32_t>("BotMinds.WorldLife.Chance.Zone", 35);
    g_JourneyTownChance = sConfigMgr->GetOption<uint32_t>("BotMinds.WorldLife.Chance.Town", 45);
    g_JourneyDungeonChance = sConfigMgr->GetOption<uint32_t>("BotMinds.WorldLife.Chance.Dungeon", 75);
    g_JourneyBossChance = sConfigMgr->GetOption<uint32_t>("BotMinds.WorldLife.Chance.Boss", 90);
    g_JourneyCooldownSec = sConfigMgr->GetOption<uint32_t>("BotMinds.WorldLife.JourneyCooldownSec", 120);
    g_ReunionChance = sConfigMgr->GetOption<uint32_t>("BotMinds.WorldLife.Reunion.Chance", 70);
    g_ReunionMinAbsenceSec = sConfigMgr->GetOption<uint32_t>(
        "BotMinds.WorldLife.Reunion.MinAbsenceSec", 21600);
    g_SharedExperienceAffinity = sConfigMgr->GetOption<float>(
        "BotMinds.WorldLife.SharedExperienceAffinity", 0.01f);
    g_IdleGestureChance = sConfigMgr->GetOption<uint32_t>("BotMinds.WorldLife.IdleGestureChance", 20);

    g_EnableEmoteReactions = sConfigMgr->GetOption<bool>("BotMinds.EmoteReaction.Enable", true);
    g_EmoteReactionChance = sConfigMgr->GetOption<uint32_t>("BotMinds.EmoteReaction.Chance", 60);
    g_EmoteReactionMirrorWeight = sConfigMgr->GetOption<uint32_t>(
        "BotMinds.EmoteReaction.MirrorWeight", 55);
    g_EmoteReactionCounterWeight = sConfigMgr->GetOption<uint32_t>(
        "BotMinds.EmoteReaction.CounterWeight", 30);
    g_EmoteReactionSpeakWeight = sConfigMgr->GetOption<uint32_t>(
        "BotMinds.EmoteReaction.SpeakWeight", 15);
    g_EmoteReactionCooldownSec = sConfigMgr->GetOption<uint32_t>(
        "BotMinds.EmoteReaction.CooldownSec", 20);

    g_ActionsEnable         = sConfigMgr->GetOption<bool>("BotMinds.Actions.Enable", true);
    g_ActionMaxAttempts     = sConfigMgr->GetOption<uint32_t>("BotMinds.Actions.MaxAttempts", 3);
    g_GiftMinAffinity       = sConfigMgr->GetOption<float>("BotMinds.Actions.Gold.MinAffinity", 0.25f);
    g_GiftMaxCopper         = sConfigMgr->GetOption<uint32_t>("BotMinds.Actions.Gold.MaxCopper", 5000);
    g_GiftCopperPerLevel    = sConfigMgr->GetOption<uint32_t>("BotMinds.Actions.Gold.CopperPerLevel", 100);
    g_GiftCooldownSec       = sConfigMgr->GetOption<uint32_t>("BotMinds.Actions.Gold.CooldownSec", 86400);
    g_UnpromptedChance      = sConfigMgr->GetOption<uint32_t>("BotMinds.Actions.Unprompted.Chance", 20);
    g_UnpromptedCooldownSec = sConfigMgr->GetOption<uint32_t>("BotMinds.Actions.Unprompted.CooldownSec", 900);
    g_ConversationHoldSec   = sConfigMgr->GetOption<uint32_t>("BotMinds.Conversation.HoldStillSec", 8);
    g_EmoteCooldownSec      = sConfigMgr->GetOption<uint32_t>("BotMinds.Emote.CooldownSec", 180);

    g_RecoveryEnable = sConfigMgr->GetOption<bool>("BotMinds.Recovery.Enable", true);
    g_RecoveryScanIntervalSec = sConfigMgr->GetOption<uint32_t>("BotMinds.Recovery.ScanIntervalSec", 10);
    g_RecoveryDistance = sConfigMgr->GetOption<float>("BotMinds.Recovery.Distance", 30.0f);
    g_RecoveryResurrectChance = sConfigMgr->GetOption<uint32_t>("BotMinds.Recovery.ResurrectChance", 75);
    g_RecoveryHealChance = sConfigMgr->GetOption<uint32_t>("BotMinds.Recovery.HealChance", 35);
    g_RecoveryHealBelowPct = sConfigMgr->GetOption<uint32_t>("BotMinds.Recovery.HealBelowPct", 55);
    g_RecoveryCooldownSec = sConfigMgr->GetOption<uint32_t>("BotMinds.Recovery.CooldownSec", 300);

    g_ReciprocityEnable = sConfigMgr->GetOption<bool>("BotMinds.Reciprocity.Enable", true);
    g_ReciprocityAffinityGain = sConfigMgr->GetOption<float>("BotMinds.Reciprocity.AffinityGain", 0.02f);
    g_ReciprocityCooldownSec = sConfigMgr->GetOption<uint32_t>("BotMinds.Reciprocity.CooldownSec", 300);
    g_ReciprocitySpeakChance = sConfigMgr->GetOption<uint32_t>("BotMinds.Reciprocity.SpeakChance", 35);

    g_EnableTypingSimulation       = sConfigMgr->GetOption<bool>("BotMinds.Typing.Enable", false);
    g_TypingSimulationBaseDelay    = sConfigMgr->GetOption<uint32_t>("BotMinds.Typing.BaseDelayMs", 1000);
    g_TypingSimulationDelayPerChar = sConfigMgr->GetOption<uint32_t>("BotMinds.Typing.DelayPerCharMs", 25);
    g_TypingSimulationMaxDelay     = sConfigMgr->GetOption<uint32_t>("BotMinds.Typing.MaxDelayMs", 8000);

    g_SaveIntervalMinutes = sConfigMgr->GetOption<uint32_t>("BotMinds.SaveIntervalMinutes", 10);

    g_BlacklistCommands = SplitString(sConfigMgr->GetOption<std::string>("BotMinds.BlacklistCommands", ""), ',');

    if (g_MaxBotsToPick == 0)
        g_MaxBotsToPick = 1;
    if (g_ActionMaxAttempts == 0)
        g_ActionMaxAttempts = 1;
    if (g_MaxConcurrentCalls == 0)
        g_MaxConcurrentCalls = 1;
    if (g_DispatchWorkerThreads == 0)
        g_DispatchWorkerThreads = 1;
    if (g_DispatchWorkerThreads > 64)
        g_DispatchWorkerThreads = 64;
    if (g_AmbientMaxIntervalSec < g_AmbientMinIntervalSec)
        g_AmbientMaxIntervalSec = g_AmbientMinIntervalSec;
    if (g_RecoveryScanIntervalSec == 0)
        g_RecoveryScanIntervalSec = 1;
    g_RecoveryResurrectChance = std::min<uint32_t>(g_RecoveryResurrectChance, 100);
    g_RecoveryHealChance = std::min<uint32_t>(g_RecoveryHealChance, 100);
    g_RecoveryHealBelowPct = std::min<uint32_t>(g_RecoveryHealBelowPct, 100);
    g_ReciprocityAffinityGain = std::clamp(g_ReciprocityAffinityGain, 0.0f, 1.0f);
    g_ReciprocitySpeakChance = std::min<uint32_t>(g_ReciprocitySpeakChance, 100);
    g_RepetitionSimilarityThreshold = std::clamp(g_RepetitionSimilarityThreshold, 0.0f, 1.0f);
    g_JourneyZoneChance = std::min<uint32_t>(g_JourneyZoneChance, 100);
    g_JourneyTownChance = std::min<uint32_t>(g_JourneyTownChance, 100);
    g_JourneyDungeonChance = std::min<uint32_t>(g_JourneyDungeonChance, 100);
    g_JourneyBossChance = std::min<uint32_t>(g_JourneyBossChance, 100);
    g_ReunionChance = std::min<uint32_t>(g_ReunionChance, 100);
    g_SharedExperienceAffinity = std::clamp(g_SharedExperienceAffinity, 0.0f, 0.1f);
    g_IdleGestureChance = std::min<uint32_t>(g_IdleGestureChance, 100);
}

BotMindsConfigWorldScript::BotMindsConfigWorldScript() : WorldScript("BotMindsConfigWorldScript") { }

void BotMindsConfigWorldScript::OnStartup()
{
    LoadBotMindsConfig();

    InitLLMProviders();
    LoadPersonasFromDB();
    LoadMemoriesFromDB();
    LoadRelationshipsFromDB();
    BotMindsDispatch_Start();

    g_LastSaveTime = time(nullptr);
}

void BotMindsConfigWorldScript::OnShutdown()
{
    BotMindsDispatch_Stop();
    FlushMemoryReferences();
    FlushPersonasToDB();
    FlushRelationshipsToDB();
}

void BotMindsConfigWorldScript::OnUpdate(uint32 diff)
{
    BotMindsGovernor::Tick(diff);

    // API work completes off-thread, but every interaction with world objects
    // is delivered here on the world thread.
    BotMindsDispatch_Update();

    // Actions returned by the model are executed here, on the world thread,
    // which is the only safe place to cast spells or open trade windows.
    RunPendingActions(diff);
    RunConversationHolds(diff);

    time_t now = time(nullptr);
    if (g_SaveIntervalMinutes > 0 && now - g_LastSaveTime >= static_cast<time_t>(g_SaveIntervalMinutes * 60))
    {
        FlushPersonasToDB();
        FlushRelationshipsToDB();
        PruneTranscripts();
        g_LastSaveTime = now;
    }
}
