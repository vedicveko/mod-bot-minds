#include "mod-bot-minds_dispatch.h"
#include "mod-bot-minds_config.h"
#include "mod-bot-minds_governor.h"

#include "Log.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;

    struct Completion
    {
        BotMindsDispatchRequest request;
        LLMResult result;
        Clock::time_point deliverAt;
    };

    std::mutex g_QueueMutex;
    std::condition_variable g_QueueCondition;
    std::deque<BotMindsDispatchRequest> g_Queue;
    bool g_Running = false;

    std::mutex g_CompletionMutex;
    std::deque<Completion> g_Completions;

    std::vector<std::thread> g_Workers;

    std::atomic<uint32_t> g_InFlight{0};
    std::atomic<uint64_t> g_Submitted{0};
    std::atomic<uint64_t> g_Completed{0};
    std::atomic<uint64_t> g_Failed{0};
    std::atomic<uint64_t> g_DroppedQueueFull{0};
    std::atomic<uint64_t> g_LastLatencyMs{0};

    std::mutex g_ErrorMutex;
    std::string g_LastError;

    void RecordError(std::string const& error)
    {
        ++g_Failed;
        std::lock_guard<std::mutex> lock(g_ErrorMutex);
        g_LastError = error;
    }

    uint32_t TypingDelay(BotMindsDispatchRequest const& request, LLMResult const& result)
    {
        if (!request.simulateTyping || result.reply.empty())
            return 0;

        uint64_t const delay = static_cast<uint64_t>(request.typingBaseDelayMs)
            + static_cast<uint64_t>(result.reply.size()) * request.typingDelayPerCharMs;
        uint64_t capped = std::min<uint64_t>(delay, std::numeric_limits<uint32_t>::max());
        if (request.typingMaxDelayMs > 0)
            capped = std::min<uint64_t>(capped, request.typingMaxDelayMs);
        return static_cast<uint32_t>(capped);
    }

    void WorkerLoop()
    {
        while (true)
        {
            BotMindsDispatchRequest request;
            {
                std::unique_lock<std::mutex> lock(g_QueueMutex);
                g_QueueCondition.wait(lock, [] { return !g_Running || !g_Queue.empty(); });

                if (!g_Running && g_Queue.empty())
                    return;

                request = std::move(g_Queue.front());
                g_Queue.pop_front();
            }

            ++g_InFlight;
            auto const started = Clock::now();

            LLMResult result;
            try
            {
                result = request.provider->Complete(request.systemPrompt, request.userPrompt);
            }
            catch (std::exception const& exception)
            {
                result.error = std::string("provider exception: ") + exception.what();
            }

            auto const finished = Clock::now();
            uint64_t const latency = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(finished - started).count());
            g_LastLatencyMs.store(latency);

            if (request.holdsGovernorSlot)
                BotMindsGovernor::OnComplete();

            if (!result.ok)
            {
                std::string error = result.error.empty() ? "provider returned no usable response" : result.error;
                std::string const labeled = request.label.empty() ? error : request.label + ": " + error;
                RecordError(labeled);
                LOG_ERROR("server.loading", "[BotMinds] Provider request failed: {}", labeled);
            }

            Completion completion;
            completion.deliverAt = finished + std::chrono::milliseconds(TypingDelay(request, result));
            completion.request = std::move(request);
            completion.result = std::move(result);

            {
                std::lock_guard<std::mutex> lock(g_CompletionMutex);
                g_Completions.push_back(std::move(completion));
            }

            ++g_Completed;
            --g_InFlight;
        }
    }
}

void BotMindsDispatch_Start()
{
    std::lock_guard<std::mutex> lock(g_QueueMutex);
    if (g_Running)
        return;

    g_Running = true;
    g_Workers.reserve(g_DispatchWorkerThreads);
    for (uint32_t i = 0; i < g_DispatchWorkerThreads; ++i)
        g_Workers.emplace_back(WorkerLoop);

    LOG_INFO("server.loading", "[BotMinds] Dispatcher started with {} worker threads.",
             g_DispatchWorkerThreads);
}

void BotMindsDispatch_Stop()
{
    std::deque<BotMindsDispatchRequest> cancelled;
    {
        std::lock_guard<std::mutex> lock(g_QueueMutex);
        if (!g_Running)
            return;

        g_Running = false;
        cancelled.swap(g_Queue);
    }

    for (BotMindsDispatchRequest const& request : cancelled)
    {
        if (request.holdsGovernorSlot)
            BotMindsGovernor::OnComplete();
    }

    g_QueueCondition.notify_all();
    for (std::thread& worker : g_Workers)
    {
        if (worker.joinable())
            worker.join();
    }
    g_Workers.clear();

    {
        std::lock_guard<std::mutex> lock(g_CompletionMutex);
        g_Completions.clear();
    }

    LOG_INFO("server.loading", "[BotMinds] Dispatcher stopped.");
}

void BotMindsDispatch_Update()
{
    auto const now = Clock::now();
    std::vector<Completion> due;

    {
        std::lock_guard<std::mutex> lock(g_CompletionMutex);
        for (auto iterator = g_Completions.begin(); iterator != g_Completions.end();)
        {
            if (iterator->deliverAt <= now)
            {
                due.push_back(std::move(*iterator));
                iterator = g_Completions.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
    }

    for (Completion& completion : due)
    {
        try
        {
            completion.request.onComplete(std::move(completion.result));
        }
        catch (std::exception const& exception)
        {
            RecordError(std::string("completion exception: ") + exception.what());
            LOG_ERROR("server.loading", "[BotMinds] Completion exception: {}", exception.what());
        }
    }
}

bool BotMindsDispatch_Submit(BotMindsDispatchRequest request)
{
    if (!request.provider || request.systemPrompt.empty() || !request.onComplete)
        return false;

    {
        std::lock_guard<std::mutex> lock(g_QueueMutex);
        if (!g_Running)
            return false;
        if (g_MaxQueueDepth > 0 && g_Queue.size() >= g_MaxQueueDepth)
        {
            ++g_DroppedQueueFull;
            return false;
        }

        g_Queue.push_back(std::move(request));
        try
        {
            if (g_Queue.back().holdsGovernorSlot)
                BotMindsGovernor::OnSubmit(g_Queue.back().governorBotGuid,
                                           g_Queue.back().governorScopeKey);
        }
        catch (...)
        {
            g_Queue.pop_back();
            throw;
        }
    }

    ++g_Submitted;
    g_QueueCondition.notify_one();
    return true;
}

bool BotMindsDispatch_SubmitTest(std::string prompt)
{
    LLMProviderPtr provider = GetProvider();
    if (!provider || prompt.empty())
        return false;

    BotMindsDispatchRequest request;
    request.provider = std::move(provider);
    request.label = "diagnostic test";
    request.systemPrompt =
        "You are testing the Bot Minds provider. Reply briefly and use the bot_turn tool for the entire response.";
    request.userPrompt = std::move(prompt);
    request.onComplete = [](LLMResult&& result)
    {
        if (!result.ok)
        {
            LOG_ERROR("server.loading", "[BotMinds] Test failed: {}",
                      result.error.empty() ? "no usable response" : result.error);
            return;
        }

        LOG_INFO("server.loading", "[BotMinds] Test response: {}",
                 result.reply.empty() ? "<model chose not to reply>" : result.reply);
    };
    return BotMindsDispatch_Submit(std::move(request));
}

BotMindsDispatchStats BotMindsDispatch_GetStats()
{
    BotMindsDispatchStats stats;
    {
        std::lock_guard<std::mutex> lock(g_QueueMutex);
        stats.workers = static_cast<uint32_t>(g_Workers.size());
        stats.queued = static_cast<uint32_t>(g_Queue.size());
    }
    {
        std::lock_guard<std::mutex> lock(g_CompletionMutex);
        stats.awaitingDelivery = static_cast<uint32_t>(g_Completions.size());
    }
    {
        std::lock_guard<std::mutex> lock(g_ErrorMutex);
        stats.lastError = g_LastError;
    }

    stats.inFlight = g_InFlight.load();
    stats.submitted = g_Submitted.load();
    stats.completed = g_Completed.load();
    stats.failed = g_Failed.load();
    stats.droppedQueueFull = g_DroppedQueueFull.load();
    stats.lastLatencyMs = g_LastLatencyMs.load();
    return stats;
}
