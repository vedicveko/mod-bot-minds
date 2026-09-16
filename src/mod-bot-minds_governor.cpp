#include "mod-bot-minds_governor.h"
#include "mod-bot-minds_config.h"
#include "mod-bot-minds_llmclient.h"

#include "Log.h"
#include "Player.h"
#include "Playerbots.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <deque>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    using GramCounts = std::unordered_map<std::string, uint32_t>;

    struct Utterance
    {
        std::string normalized;
        std::string opener;
        std::set<std::string> tokens;
        GramCounts grams;
        double gramNorm = 0.0;
        uint32_t atSec = 0;
    };

    struct BotGovernorState
    {
        uint32_t lastCallSec = 0;
        std::deque<Utterance> history;
    };

    struct ScopeState
    {
        uint32_t lastCallSec = 0;
        std::deque<uint32_t> callTimes;
        std::deque<Utterance> history;
    };

    std::atomic<int>      g_InFlight{0};
    std::atomic<uint32_t> g_CallsThisMinute{0};
    std::atomic<uint32_t> g_CallsTotal{0};
    std::atomic<uint32_t> g_RepetitionsBlocked{0};
    std::atomic<uint32_t> g_PacingBlocked{0};
    uint32_t              g_MinuteElapsedMs = 0;

    std::mutex g_StateMutex;
    std::unordered_map<uint64_t, BotGovernorState> g_Bots;
    std::unordered_map<ScopeKey, ScopeState, ScopeKeyHash> g_Scopes;

    bool IsBot(Player* player)
    {
        return player && GET_PLAYERBOT_AI(player);
    }

    bool ScopeNeedsProximity(ChatScope scope)
    {
        return scope == ChatScope::Say;
    }

    uint32_t NowSec()
    {
        return static_cast<uint32_t>(time(nullptr));
    }

    uint32_t Age(uint32_t then, uint32_t now)
    {
        return then == 0 ? UINT32_MAX : now - then;
    }

    void TrimCallWindow(std::deque<uint32_t>& calls, uint32_t now)
    {
        while (!calls.empty() && Age(calls.front(), now) >= 60)
            calls.pop_front();
    }

    std::unordered_set<std::string> const& Stopwords()
    {
        static std::unordered_set<std::string> const words = {
            "a", "an", "the", "and", "or", "but", "if", "of", "to", "in", "on", "at", "for",
            "is", "are", "was", "were", "be", "been", "am", "i", "you", "he", "she", "it",
            "we", "they", "me", "my", "your", "this", "that", "these", "those", "so", "just",
            "really", "very", "gonna", "got", "get", "do", "does", "did", "have", "has", "had",
            "will", "would", "can", "could", "should", "not", "no", "yes"
        };
        return words;
    }

    std::string NormalizeText(std::string const& text)
    {
        std::string normalized;
        normalized.reserve(text.size());

        for (unsigned char character : text)
        {
            if (std::isalnum(character))
                normalized.push_back(static_cast<char>(std::tolower(character)));
            else if (!normalized.empty() && normalized.back() != ' ')
                normalized.push_back(' ');
        }

        while (!normalized.empty() && normalized.back() == ' ')
            normalized.pop_back();

        return normalized;
    }

    std::vector<std::string> Tokenize(std::string const& normalized, bool dropStopwords)
    {
        std::vector<std::string> tokens;
        size_t start = 0;

        while (start < normalized.size())
        {
            size_t end = normalized.find(' ', start);
            if (end == std::string::npos)
                end = normalized.size();

            std::string token = normalized.substr(start, end - start);
            if (!token.empty() && (!dropStopwords || Stopwords().count(token) == 0))
                tokens.push_back(std::move(token));

            start = end + 1;
        }

        return tokens;
    }

    std::string OpenerOf(std::string const& normalized)
    {
        std::vector<std::string> const tokens = Tokenize(normalized, false);
        std::string opener;
        for (size_t index = 0; index < tokens.size() && index < 3; ++index)
        {
            if (!opener.empty())
                opener.push_back(' ');
            opener += tokens[index];
        }
        return opener;
    }

    std::set<std::string> BuildTokenSet(std::string const& normalized)
    {
        std::set<std::string> tokens;
        for (std::string& token : Tokenize(normalized, true))
            tokens.insert(std::move(token));
        return tokens;
    }

    GramCounts BuildGrams(std::string const& text)
    {
        GramCounts grams;
        if (text.size() < 3)
        {
            if (!text.empty())
                grams[text] = 1;
            return grams;
        }

        for (size_t index = 0; index + 3 <= text.size(); ++index)
            ++grams[text.substr(index, 3)];
        return grams;
    }

    double GramNorm(GramCounts const& grams)
    {
        double squared = 0.0;
        for (auto const& entry : grams)
            squared += static_cast<double>(entry.second) * entry.second;
        return std::sqrt(squared);
    }

    float Jaccard(std::set<std::string> const& first, std::set<std::string> const& second)
    {
        if (first.empty() || second.empty())
            return 0.0f;

        size_t intersection = 0;
        for (std::string const& token : first)
            if (second.count(token) != 0)
                ++intersection;

        size_t const combined = first.size() + second.size() - intersection;
        return combined == 0 ? 0.0f : static_cast<float>(intersection) / combined;
    }

    float Cosine(GramCounts const& first, double firstNorm,
                 GramCounts const& second, double secondNorm)
    {
        if (firstNorm <= 0.0 || secondNorm <= 0.0)
            return 0.0f;

        GramCounts const& fewer = first.size() <= second.size() ? first : second;
        GramCounts const& more = first.size() <= second.size() ? second : first;
        double dot = 0.0;

        for (auto const& entry : fewer)
        {
            auto found = more.find(entry.first);
            if (found != more.end())
                dot += static_cast<double>(entry.second) * found->second;
        }

        return static_cast<float>(dot / (firstNorm * secondNorm));
    }

    bool SimilarToHistory(std::deque<Utterance> const& history, std::string const& normalized,
                          std::set<std::string> const& tokens, GramCounts const& grams,
                          double gramNorm, uint32_t now)
    {
        for (Utterance const& utterance : history)
        {
            if (Age(utterance.atSec, now) > g_RepetitionWindowSec)
                continue;
            if (utterance.normalized == normalized)
                return true;

            size_t const shorter = std::min(normalized.size(), utterance.normalized.size());
            size_t const longer = std::max(normalized.size(), utterance.normalized.size());
            if (longer > 0 && static_cast<double>(shorter) / longer < 0.4)
                continue;

            if (Jaccard(tokens, utterance.tokens) >= g_RepetitionSimilarityThreshold)
                return true;
            if (Cosine(grams, gramNorm, utterance.grams, utterance.gramNorm)
                >= g_RepetitionSimilarityThreshold)
            {
                return true;
            }
        }

        return false;
    }

    void TrimHistory(std::deque<Utterance>& history, size_t limit)
    {
        while (history.size() > limit)
            history.pop_front();
    }
}

namespace BotMindsGovernor
{
    bool Allow(Player* bot, Player* addresser, ScopeKey const& key, bool priority)
    {
        if (GetProvider() == nullptr || !IsBot(bot))
            return false;

        if (g_MaxCallsPerMinute > 0 && g_CallsThisMinute.load() >= g_MaxCallsPerMinute)
        {
            if (g_DebugEnabled)
            {
                LOG_INFO("server.loading", "[BotMinds] {} silent: this minute's cap of {} calls is used up",
                         bot->GetName(), g_MaxCallsPerMinute);
            }
            return false;
        }

        if (g_InFlight.load() >= static_cast<int>(g_MaxConcurrentCalls))
            return false;

        if (!priority)
        {
            uint32_t const now = NowSec();
            std::lock_guard<std::mutex> lock(g_StateMutex);

            BotGovernorState const& botState = g_Bots[bot->GetGUID().GetRawValue()];
            if (g_PerBotCooldownSec > 0 && Age(botState.lastCallSec, now) < g_PerBotCooldownSec)
            {
                ++g_PacingBlocked;
                return false;
            }

            ScopeState& scopeState = g_Scopes[key];
            if (g_PerScopeCooldownSec > 0 && Age(scopeState.lastCallSec, now) < g_PerScopeCooldownSec)
            {
                ++g_PacingBlocked;
                return false;
            }

            TrimCallWindow(scopeState.callTimes, now);
            if (g_MaxCallsPerScopePerMinute > 0
                && scopeState.callTimes.size() >= g_MaxCallsPerScopePerMinute)
            {
                ++g_PacingBlocked;
                return false;
            }
        }

        if (!priority && addresser && ScopeNeedsProximity(key.scope)
            && !bot->IsWithinDistInMap(addresser, g_ProximityRadius))
        {
            return false;
        }

        return true;
    }

    void OnSubmit(uint64_t botGuid, ScopeKey const& key)
    {
        uint32_t const now = NowSec();
        {
            std::lock_guard<std::mutex> lock(g_StateMutex);
            g_Bots[botGuid].lastCallSec = now;
            ScopeState& scope = g_Scopes[key];
            scope.lastCallSec = now;
            TrimCallWindow(scope.callTimes, now);
            scope.callTimes.push_back(now);
        }

        ++g_InFlight;
        ++g_CallsThisMinute;
        ++g_CallsTotal;
    }

    void OnComplete()
    {
        --g_InFlight;
    }

    uint32_t CallsSinceStartup()
    {
        return g_CallsTotal.load();
    }

    int CallsInFlight()
    {
        return g_InFlight.load();
    }

    bool IsRepetitive(uint64_t botGuid, ScopeKey const& key, std::string const& text)
    {
        if ((g_BotHistorySize == 0 && g_ScopeHistorySize == 0)
            || g_RepetitionWindowSec == 0 || text.empty())
        {
            return false;
        }

        std::string const normalized = NormalizeText(text);
        if (normalized.empty())
            return false;

        std::set<std::string> const tokens = BuildTokenSet(normalized);
        GramCounts const grams = BuildGrams(normalized);
        double const gramNorm = GramNorm(grams);
        std::string const opener = OpenerOf(normalized);
        uint32_t const now = NowSec();

        std::lock_guard<std::mutex> lock(g_StateMutex);

        auto bot = g_Bots.find(botGuid);
        if (bot != g_Bots.end()
            && SimilarToHistory(bot->second.history, normalized, tokens, grams, gramNorm, now))
        {
            ++g_RepetitionsBlocked;
            return true;
        }

        auto scope = g_Scopes.find(key);
        if (scope == g_Scopes.end())
            return false;

        if (SimilarToHistory(scope->second.history, normalized, tokens, grams, gramNorm, now))
        {
            ++g_RepetitionsBlocked;
            return true;
        }

        if (g_OpenerHistorySize > 0 && !opener.empty())
        {
            uint32_t checked = 0;
            for (auto it = scope->second.history.rbegin();
                 it != scope->second.history.rend() && checked < g_OpenerHistorySize; ++it, ++checked)
            {
                if (Age(it->atSec, now) <= g_RepetitionWindowSec && it->opener == opener)
                {
                    ++g_RepetitionsBlocked;
                    return true;
                }
            }
        }

        return false;
    }

    void RecordUtterance(uint64_t botGuid, ScopeKey const& key, std::string const& text)
    {
        std::string const normalized = NormalizeText(text);
        if (normalized.empty())
            return;

        Utterance utterance;
        utterance.normalized = normalized;
        utterance.opener = OpenerOf(normalized);
        utterance.tokens = BuildTokenSet(normalized);
        utterance.grams = BuildGrams(normalized);
        utterance.gramNorm = GramNorm(utterance.grams);
        utterance.atSec = NowSec();

        std::lock_guard<std::mutex> lock(g_StateMutex);
        if (g_BotHistorySize > 0)
        {
            std::deque<Utterance>& history = g_Bots[botGuid].history;
            history.push_back(utterance);
            TrimHistory(history, g_BotHistorySize);
        }
        if (g_ScopeHistorySize > 0)
        {
            std::deque<Utterance>& history = g_Scopes[key].history;
            history.push_back(std::move(utterance));
            TrimHistory(history, g_ScopeHistorySize);
        }
    }

    uint32_t RepetitionsBlocked()
    {
        return g_RepetitionsBlocked.load();
    }

    uint32_t PacingBlocked()
    {
        return g_PacingBlocked.load();
    }

    void Tick(uint32_t diffMs)
    {
        g_MinuteElapsedMs += diffMs;
        if (g_MinuteElapsedMs < 60000)
            return;

        g_CallsThisMinute.store(0);
        g_MinuteElapsedMs %= 60000;

        uint32_t const now = NowSec();
        std::lock_guard<std::mutex> lock(g_StateMutex);

        for (auto& entry : g_Bots)
        {
            std::deque<Utterance>& history = entry.second.history;
            while (!history.empty() && Age(history.front().atSec, now) > g_RepetitionWindowSec)
                history.pop_front();
        }

        for (auto it = g_Scopes.begin(); it != g_Scopes.end();)
        {
            ScopeState& scope = it->second;
            TrimCallWindow(scope.callTimes, now);
            while (!scope.history.empty() && Age(scope.history.front().atSec, now) > g_RepetitionWindowSec)
                scope.history.pop_front();

            bool const stale = scope.callTimes.empty() && scope.history.empty()
                && Age(scope.lastCallSec, now) > std::max<uint32_t>(600, g_RepetitionWindowSec * 2);
            if (stale)
                it = g_Scopes.erase(it);
            else
                ++it;
        }
    }
}
