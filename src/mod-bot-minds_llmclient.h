#ifndef MOD_BOT_MINDS_LLMCLIENT_H
#define MOD_BOT_MINDS_LLMCLIENT_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

struct LLMResult
{
    bool ok = false;
    bool shouldReply = true;
    std::string reply;
    std::string emote;
    std::string error;
    nlohmann::json memory_additions = nlohmann::json::array();
    nlohmann::json relationship_delta = nullptr;
    nlohmann::json action = nullptr;
};

class ILLMProvider
{
public:
    virtual ~ILLMProvider() = default;
    virtual LLMResult Complete(std::string const& systemPrompt, std::string const& userPrompt) const = 0;
};

using LLMProviderPtr = std::shared_ptr<ILLMProvider const>;

// Trim and clean a raw model reply: strip quoting, emotes and newlines, then cut
// to maxChars on a sentence or word boundary rather than mid-word.
std::string SanitizeReply(std::string reply, size_t maxChars);

// Replaces the provider atomically. Requests already queued retain the provider
// snapshot they started with, so a live reload cannot invalidate their client.
void InitLLMProviders();
LLMProviderPtr GetProvider();

#endif // MOD_BOT_MINDS_LLMCLIENT_H
