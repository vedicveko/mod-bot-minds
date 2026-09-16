#ifndef MOD_BOT_MINDS_DISPATCH_H
#define MOD_BOT_MINDS_DISPATCH_H

#include "mod-bot-minds_llmclient.h"
#include "mod-bot-minds_transcript.h"

#include <cstdint>
#include <functional>
#include <string>

struct BotMindsDispatchRequest
{
    LLMProviderPtr provider;
    std::string systemPrompt;
    std::string userPrompt;
    std::string label;
    std::function<void(LLMResult&&)> onComplete;
    uint64_t governorBotGuid = 0;
    ScopeKey governorScopeKey;
    bool holdsGovernorSlot = false;
    bool simulateTyping = false;
    uint32_t typingBaseDelayMs = 0;
    uint32_t typingDelayPerCharMs = 0;
    uint32_t typingMaxDelayMs = 0;
};

struct BotMindsDispatchStats
{
    uint32_t workers = 0;
    uint32_t queued = 0;
    uint32_t inFlight = 0;
    uint32_t awaitingDelivery = 0;
    uint64_t submitted = 0;
    uint64_t completed = 0;
    uint64_t failed = 0;
    uint64_t droppedQueueFull = 0;
    uint64_t lastLatencyMs = 0;
    std::string lastError;
};

void BotMindsDispatch_Start();
void BotMindsDispatch_Stop();
void BotMindsDispatch_Update();
bool BotMindsDispatch_Submit(BotMindsDispatchRequest request);
bool BotMindsDispatch_SubmitTest(std::string prompt);
BotMindsDispatchStats BotMindsDispatch_GetStats();

#endif // MOD_BOT_MINDS_DISPATCH_H
