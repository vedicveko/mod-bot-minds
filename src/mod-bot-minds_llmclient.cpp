#include "mod-bot-minds_llmclient.h"
#include "mod-bot-minds_config.h"
#include "mod-bot-minds_httpclient.h"

#include "Log.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <utility>
#include <vector>

using nlohmann::json;

namespace
{
    struct ProviderConfig
    {
        std::string apiKey;
        std::string model;
        std::string url;
        uint32_t maxTokens = 512;
        uint32_t timeoutSeconds = 30;
        uint32_t maxReplyChars = 200;
        bool debug = false;
    };

    json const& BotTurnSchema()
    {
        static json const schema = {
            {"type", "object"},
            {"properties", {
                {"should_reply", {
                    {"type", "boolean"},
                    {"description", "False to stay silent, for example when the message was meant for someone else."}
                }},
                {"reply", {
                    {"type", "string"},
                    {"description", "The words the bot says out loud. Empty when should_reply is false."}
                }},
                {"emote", {
                    {"type", "string"},
                    {"description", "Optional gesture to go with the line: wave, laugh, nod, shrug, thank, "
                                    "cheer, salute, bow or sigh. Use these rarely. An occasional wave says "
                                    "something; one attached to every greeting is noise, and most lines want "
                                    "none at all."}
                }},
                {"memory_additions", {
                    {"type", "array"},
                    {"description", "Anything from this exchange worth remembering later. Durable "
                                    "things only: who someone is, what they did, how it went. Not "
                                    "passing state such as which buffs they have up, their health, "
                                    "where they are or who is with them, which is wrong within "
                                    "minutes of writing it down."},
                    {"items", {
                        {"type", "object"},
                        {"properties", {
                            {"kind", {{"type", "string"}, {"enum", json::array({"event", "fact"})}}},
                            {"text", {{"type", "string"}}},
                            {"salience", {{"type", "number"}}}
                        }},
                        {"required", json::array({"kind", "text", "salience"})}
                    }}
                }},
                {"relationship_delta", {
                    {"type", "object"},
                    {"description", "How this exchange changes the bot's feelings toward the other person."},
                    {"properties", {
                        {"affinity_change", {{"type", "number"}}},
                        {"reason", {{"type", "string"}}}
                    }},
                    {"required", json::array({"affinity_change", "reason"})}
                }},
                {"action", {
                    {"type", "object"},
                    {"description", "Something the bot actually does, alongside saying its line. Fill this "
                                    "in whenever the list of what you can do covers what was asked: agreeing "
                                    "in words without setting it means nothing happens in the game. Leave it "
                                    "out only when you genuinely cannot do the thing."},
                    {"properties", {
                        {"kind", {
                            {"type", "string"},
                            {"enum", json::array({"none", "buff", "heal", "give_gold", "follow", "stay"})}
                        }},
                        {"spell", {
                            {"type", "string"},
                            {"description", "For buff or heal: the exact name from the list you were offered."}
                        }},
                        {"copper", {
                            {"type", "integer"},
                            {"description", "For give_gold: the amount in copper, never more than you were told "
                                            "you would spare."}
                        }}
                    }},
                    {"required", json::array({"kind"})}
                }}
            }},
            {"required", json::array({"should_reply", "reply"})}
        };
        return schema;
    }

    json BotTurnToolAnthropic()
    {
        return {
            {"name", "bot_turn"},
            {"description", "Record the bot's spoken reply along with any new memories and relationship changes."},
            {"input_schema", BotTurnSchema()}
        };
    }

    json BotTurnToolOpenAI()
    {
        json parameters = BotTurnSchema();
        parameters["additionalProperties"] = false;

        return {
            {"type", "function"},
            {"function", {
                {"name", "bot_turn"},
                {"description", "Record the bot's spoken reply along with any new memories and relationship "
                                "changes."},
                {"parameters", std::move(parameters)}
            }}
        };
    }

    void TrimInPlace(std::string& value)
    {
        size_t const start = value.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
        {
            value.clear();
            return;
        }

        size_t const end = value.find_last_not_of(" \t\r\n");
        value = value.substr(start, end - start + 1);
    }

    uint32_t DecodeUtf8(std::string const& text, size_t& index)
    {
        unsigned char const first = static_cast<unsigned char>(text[index]);
        if (first <= 0x7F)
        {
            ++index;
            return first;
        }

        auto continuation = [&](size_t offset)
        {
            return index + offset < text.size()
                && (static_cast<unsigned char>(text[index + offset]) & 0xC0) == 0x80;
        };

        if ((first & 0xE0) == 0xC0 && continuation(1))
        {
            uint32_t const codepoint = ((first & 0x1Fu) << 6)
                | (static_cast<unsigned char>(text[index + 1]) & 0x3Fu);
            index += 2;
            return codepoint;
        }
        if ((first & 0xF0) == 0xE0 && continuation(1) && continuation(2))
        {
            uint32_t const codepoint = ((first & 0x0Fu) << 12)
                | ((static_cast<unsigned char>(text[index + 1]) & 0x3Fu) << 6)
                | (static_cast<unsigned char>(text[index + 2]) & 0x3Fu);
            index += 3;
            return codepoint;
        }
        if ((first & 0xF8) == 0xF0 && continuation(1) && continuation(2) && continuation(3))
        {
            uint32_t const codepoint = ((first & 0x07u) << 18)
                | ((static_cast<unsigned char>(text[index + 1]) & 0x3Fu) << 12)
                | ((static_cast<unsigned char>(text[index + 2]) & 0x3Fu) << 6)
                | (static_cast<unsigned char>(text[index + 3]) & 0x3Fu);
            index += 4;
            return codepoint;
        }

        ++index;
        return 0xFFFD;
    }

    void AppendUtf8(std::string& output, uint32_t codepoint)
    {
        if (codepoint <= 0x7F)
        {
            output.push_back(static_cast<char>(codepoint));
        }
        else if (codepoint <= 0x7FF)
        {
            output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
        else if (codepoint <= 0xFFFF)
        {
            output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
    }

    std::string StripThinkTags(std::string text)
    {
        std::string const openTag = "<think>";
        std::string const closeTag = "</think>";

        while (true)
        {
            size_t const open = text.find(openTag);
            if (open == std::string::npos)
                break;

            size_t const close = text.find(closeTag, open + openTag.size());
            if (close == std::string::npos)
            {
                text.erase(open);
                break;
            }
            text.erase(open, close + closeTag.size() - open);
        }

        size_t const orphan = text.find(closeTag);
        if (orphan != std::string::npos)
            text.erase(0, orphan + closeTag.size());

        TrimInPlace(text);
        return text;
    }

    std::string StripStageDirections(std::string text)
    {
        while (true)
        {
            size_t const open = text.find('*');
            if (open == std::string::npos)
                break;

            size_t const close = text.find('*', open + 1);
            if (close == std::string::npos)
            {
                text.erase(open, 1);
                break;
            }

            std::string const inner = text.substr(open + 1, close - open - 1);
            bool const oneWord = !inner.empty() && inner.size() <= 24
                && inner.find_first_of(" \t\r\n") == std::string::npos;

            size_t before = open;
            while (before > 0 && std::isspace(static_cast<unsigned char>(text[before - 1])))
                --before;

            size_t after = close + 1;
            while (after < text.size() && std::isspace(static_cast<unsigned char>(text[after])))
                ++after;

            bool const wholeLine = before == 0 && after >= text.size();
            bool const atEdge = before == 0 || after >= text.size();
            if (wholeLine || (oneWord && atEdge))
            {
                text.erase(open, close - open + 1);
                continue;
            }

            // This is ordinary emphasis, not narration. Keep the words and
            // remove only the markdown markers.
            text.erase(close, 1);
            text.erase(open, 1);
        }

        TrimInPlace(text);
        return text;
    }

    std::string StripMarkdown(std::string text)
    {
        while (true)
        {
            size_t const open = text.find("```");
            if (open == std::string::npos)
                break;
            size_t const close = text.find("```", open + 3);
            if (close == std::string::npos)
            {
                text.erase(open);
                break;
            }
            text.erase(open, close + 3 - open);
        }

        std::string output;
        output.reserve(text.size());
        bool atLineStart = true;

        for (size_t index = 0; index < text.size(); ++index)
        {
            char const character = text[index];
            if (atLineStart)
            {
                if (character == '#' || character == '>')
                    continue;
                if ((character == '-' || character == '*' || character == '+')
                    && index + 1 < text.size() && text[index + 1] == ' ')
                {
                    ++index;
                    continue;
                }
                if (character != ' ' && character != '\t' && character != '\r')
                    atLineStart = false;
            }

            if (character == '\n')
            {
                atLineStart = true;
                output.push_back(character);
                continue;
            }

            if (character == '*' || character == '_' || character == '`' || character == '~')
                continue;

            output.push_back(character);
        }

        return output;
    }

    std::string StripDecorativeUnicode(std::string const& text)
    {
        std::string output;
        output.reserve(text.size());

        size_t index = 0;
        while (index < text.size())
        {
            uint32_t const codepoint = DecodeUtf8(text, index);
            switch (codepoint)
            {
                case 0x2018:
                case 0x2019:
                case 0x201B:
                    output.push_back('\'');
                    continue;
                case 0x201C:
                case 0x201D:
                case 0x201F:
                    output.push_back('"');
                    continue;
                case 0x2013:
                case 0x2014:
                case 0x2212:
                    output.push_back('-');
                    continue;
                case 0x2026:
                    output.append("...");
                    continue;
                case 0x00A0:
                case 0x2007:
                case 0x202F:
                    output.push_back(' ');
                    continue;
                default:
                    break;
            }

            if (codepoint <= 0x7F || (codepoint >= 0xA0 && codepoint <= 0x24F))
                AppendUtf8(output, codepoint);
        }

        return output;
    }

    void CollapseWhitespace(std::string& text)
    {
        std::string output;
        output.reserve(text.size());
        bool pendingSpace = false;

        for (unsigned char character : text)
        {
            bool const whitespace = std::isspace(character) != 0;
            if (whitespace)
            {
                pendingSpace = !output.empty();
                continue;
            }
            if (pendingSpace)
                output.push_back(' ');
            pendingSpace = false;
            output.push_back(static_cast<char>(character));
        }

        text = std::move(output);
    }

    std::string ClampReplyLength(std::string text, size_t maximum)
    {
        if (maximum == 0 || text.size() <= maximum)
            return text;

        size_t cut = maximum;
        while (cut > 0 && cut < text.size()
            && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
        {
            --cut;
        }
        text.resize(cut);

        size_t const sentenceEnd = text.find_last_of(".!?");
        if (sentenceEnd != std::string::npos && sentenceEnd + 1 >= maximum / 2)
            text.resize(sentenceEnd + 1);
        else
        {
            size_t const wordEnd = text.find_last_of(' ');
            if (wordEnd != std::string::npos && wordEnd >= maximum / 2)
                text.resize(wordEnd);
        }

        TrimInPlace(text);
        return text;
    }

    float ClampFloat(float value, float minimum, float maximum)
    {
        return std::max(minimum, std::min(maximum, value));
    }

    void NormalizeResult(LLMResult& result, size_t maxReplyChars)
    {
        result.reply = SanitizeReply(std::move(result.reply), maxReplyChars);

        if (result.memory_additions.is_array())
        {
            for (json& item : result.memory_additions)
            {
                if (item.is_object() && item.contains("salience") && item["salience"].is_number())
                    item["salience"] = ClampFloat(item["salience"].get<float>(), 0.0f, 1.0f);
            }
        }

        if (result.relationship_delta.is_object()
            && result.relationship_delta.contains("affinity_change")
            && result.relationship_delta["affinity_change"].is_number())
        {
            result.relationship_delta["affinity_change"] = ClampFloat(
                result.relationship_delta["affinity_change"].get<float>(), -1.0f, 1.0f);
        }
    }

    bool FillFromToolInput(LLMResult& result, json const& input)
    {
        if (!input.is_object() || !input.contains("reply") || !input["reply"].is_string()
            || !input.contains("should_reply") || !input["should_reply"].is_boolean())
        {
            return false;
        }

        result.reply = input["reply"].get<std::string>();
        result.shouldReply = input["should_reply"].get<bool>();
        if (input.contains("emote") && input["emote"].is_string())
            result.emote = input["emote"].get<std::string>();
        if (input.contains("memory_additions") && input["memory_additions"].is_array())
            result.memory_additions = input["memory_additions"];
        if (input.contains("relationship_delta") && input["relationship_delta"].is_object())
            result.relationship_delta = input["relationship_delta"];
        if (input.contains("action") && input["action"].is_object())
            result.action = input["action"];

        return true;
    }

    bool DecodeToolArguments(json const& arguments, json& output)
    {
        if (arguments.is_object())
        {
            output = arguments;
            return true;
        }

        if (!arguments.is_string())
            return false;

        output = json::parse(arguments.get<std::string>());
        return output.is_object();
    }

    bool FillFromOpenAIToolCall(LLMResult& result, json const& message)
    {
        if (!message.contains("tool_calls") || !message["tool_calls"].is_array()
            || message["tool_calls"].empty())
        {
            return false;
        }

        json const& call = message["tool_calls"][0];
        if (!call.is_object() || !call.contains("function") || !call["function"].is_object())
            return false;

        json const& function = call["function"];
        if (function.value("name", std::string()) != "bot_turn" || !function.contains("arguments"))
            return false;

        json input;
        if (!DecodeToolArguments(function["arguments"], input))
            return false;

        return FillFromToolInput(result, input);
    }

    bool FillFromTextFallback(LLMResult& result, json const& content)
    {
        if (!content.is_string())
            return false;

        std::string const text = content.get<std::string>();
        if (text.empty())
            return false;

        try
        {
            json const structured = json::parse(text);
            if (structured.is_object() && structured.contains("reply"))
                return FillFromToolInput(result, structured);
        }
        catch (json::parse_error const&)
        {
        }

        result.reply = text;
        return true;
    }

    std::string HttpError(BotMindsHttpResult const& response)
    {
        if (!response.error.empty())
            return response.error;
        if (response.status != 0)
            return "HTTP " + std::to_string(response.status) + (response.body.empty() ? "" : ": " + response.body);
        return "empty provider response";
    }

    class AnthropicProvider final : public ILLMProvider
    {
    public:
        explicit AnthropicProvider(ProviderConfig config) : _config(std::move(config)) { }

        LLMResult Complete(std::string const& systemPrompt, std::string const& userPrompt) const override
        {
            json const body = {
                {"model", _config.model},
                {"max_tokens", _config.maxTokens},
                {"system", systemPrompt},
                {"messages", json::array({json{{"role", "user"}, {"content", userPrompt}}})},
                {"tools", json::array({BotTurnToolAnthropic()})},
                {"tool_choice", json{{"type", "tool"}, {"name", "bot_turn"}}}
            };

            std::vector<std::pair<std::string, std::string>> const headers = {
                {"x-api-key", _config.apiKey},
                {"anthropic-version", "2023-06-01"},
                {"content-type", "application/json"}
            };

            BotMindsHttpResult const response = _http.Post(
                "https://api.anthropic.com/v1/messages", body.dump(), headers,
                static_cast<int>(_config.timeoutSeconds), _config.debug);

            LLMResult result;
            if (!response.Ok())
            {
                result.error = HttpError(response);
                return result;
            }

            try
            {
                json const parsed = json::parse(response.body);
                std::string fallback;
                bool filled = false;

                if (parsed.contains("content") && parsed["content"].is_array())
                {
                    for (json const& block : parsed["content"])
                    {
                        if (!block.is_object())
                            continue;

                        std::string const type = block.value("type", std::string());
                        if (type == "tool_use" && block.value("name", std::string()) == "bot_turn"
                            && block.contains("input") && block["input"].is_object())
                        {
                            filled = FillFromToolInput(result, block["input"]);
                            if (filled)
                                break;
                        }

                        if (type == "text" && fallback.empty() && block.contains("text")
                            && block["text"].is_string())
                        {
                            fallback = block["text"].get<std::string>();
                        }
                    }
                }

                if (!filled && !fallback.empty())
                    filled = FillFromTextFallback(result, fallback);

                result.ok = filled;
                if (!result.ok)
                    result.error = "Anthropic returned no usable bot_turn";
                NormalizeResult(result, _config.maxReplyChars);
            }
            catch (std::exception const& exception)
            {
                result.error = std::string("Anthropic response parse failed: ") + exception.what();
            }

            return result;
        }

    private:
        ProviderConfig _config;
        BotMindsHttpClient _http;
    };

    class OpenAIProvider final : public ILLMProvider
    {
    public:
        explicit OpenAIProvider(ProviderConfig config) : _config(std::move(config)) { }

        LLMResult Complete(std::string const& systemPrompt, std::string const& userPrompt) const override
        {
            BotMindsHttpResult response = Post(systemPrompt, userPrompt, "max_completion_tokens");
            if (!response.Ok() && response.status >= 400 && response.status < 500)
            {
                LOG_INFO("server.loading",
                         "[BotMinds] OpenAI rejected max_completion_tokens; retrying with max_tokens.");
                response = Post(systemPrompt, userPrompt, "max_tokens");
            }

            LLMResult result;
            if (!response.Ok())
            {
                result.error = HttpError(response);
                return result;
            }

            try
            {
                json const parsed = json::parse(response.body);
                if (!parsed.contains("choices") || !parsed["choices"].is_array() || parsed["choices"].empty())
                {
                    result.error = "OpenAI returned no choices";
                    return result;
                }

                json const message = parsed["choices"][0].value("message", json::object());
                bool filled = FillFromOpenAIToolCall(result, message);
                if (!filled && message.contains("content"))
                    filled = FillFromTextFallback(result, message["content"]);

                result.ok = filled;
                if (!result.ok)
                    result.error = "OpenAI returned no usable bot_turn";
                NormalizeResult(result, _config.maxReplyChars);
            }
            catch (std::exception const& exception)
            {
                result.error = std::string("OpenAI response parse failed: ") + exception.what();
            }

            return result;
        }

    private:
        BotMindsHttpResult Post(std::string const& systemPrompt, std::string const& userPrompt,
                                char const* tokenLimitParameter) const
        {
            json body = {
                {"model", _config.model},
                {"messages", json::array({
                    json{{"role", "system"}, {"content", systemPrompt}},
                    json{{"role", "user"}, {"content", userPrompt}}
                })},
                {"tools", json::array({BotTurnToolOpenAI()})},
                {"tool_choice", json{{"type", "function"}, {"function", {{"name", "bot_turn"}}}}}
            };
            body[tokenLimitParameter] = _config.maxTokens;

            std::vector<std::pair<std::string, std::string>> const headers = {
                {"Authorization", "Bearer " + _config.apiKey},
                {"content-type", "application/json"}
            };

            return _http.Post("https://api.openai.com/v1/chat/completions", body.dump(), headers,
                              static_cast<int>(_config.timeoutSeconds), _config.debug);
        }

        ProviderConfig _config;
        BotMindsHttpClient _http;
    };

    class OllamaProvider final : public ILLMProvider
    {
    public:
        explicit OllamaProvider(ProviderConfig config) : _config(std::move(config)) { }

        LLMResult Complete(std::string const& systemPrompt, std::string const& userPrompt) const override
        {
            std::string const toolInstruction =
                systemPrompt + "\n\nUse the bot_turn tool for the entire response. Do not answer outside that tool.";

            json body = {
                {"model", _config.model},
                {"messages", json::array({
                    json{{"role", "system"}, {"content", toolInstruction}},
                    json{{"role", "user"}, {"content", userPrompt}}
                })},
                {"stream", false},
                // Bot turns are short, structured tool calls. Thinking adds
                // latency and can consume num_predict before a call is emitted.
                {"think", false},
                {"tools", json::array({BotTurnToolOpenAI()})}
            };

            if (_config.maxTokens > 0)
                body["options"] = {{"num_predict", _config.maxTokens}};

            std::vector<std::pair<std::string, std::string>> headers = {
                {"content-type", "application/json"}
            };
            if (!_config.apiKey.empty())
                headers.emplace_back("Authorization", "Bearer " + _config.apiKey);

            BotMindsHttpResult const response = _http.Post(
                _config.url, body.dump(), headers, static_cast<int>(_config.timeoutSeconds), _config.debug);

            LLMResult result;
            if (!response.Ok())
            {
                result.error = HttpError(response);
                return result;
            }

            try
            {
                json const parsed = json::parse(response.body);
                if (parsed.contains("error") && parsed["error"].is_string())
                {
                    result.error = parsed["error"].get<std::string>();
                    return result;
                }

                json const message = parsed.value("message", json::object());
                bool filled = FillFromOpenAIToolCall(result, message);
                if (!filled && message.contains("content"))
                    filled = FillFromTextFallback(result, message["content"]);

                result.ok = filled;
                if (!result.ok)
                    result.error = "Ollama returned no usable bot_turn";
                NormalizeResult(result, _config.maxReplyChars);
            }
            catch (std::exception const& exception)
            {
                result.error = std::string("Ollama response parse failed: ") + exception.what();
            }

            return result;
        }

    private:
        ProviderConfig _config;
        BotMindsHttpClient _http;
    };

    std::mutex g_ProviderMutex;
    LLMProviderPtr g_Provider;

    std::string ResolveApiKey()
    {
        if (!g_CloudApiKey.empty())
            return g_CloudApiKey;

        if (g_ApiKeyEnv.empty())
            return {};

        char const* value = std::getenv(g_ApiKeyEnv.c_str());
        return value ? value : "";
    }
}

std::string SanitizeReply(std::string reply, size_t maxChars)
{
    reply = StripThinkTags(std::move(reply));
    if (reply.empty())
        return reply;

    // Stage directions are not chat. Edge actions such as "*waves* hello"
    // are removed, while normal emphasis keeps its words.
    reply = StripStageDirections(std::move(reply));

    if (g_StripMarkdown)
        reply = StripMarkdown(std::move(reply));

    if (g_StripDecorativeUnicode)
        reply = StripDecorativeUnicode(reply);

    CollapseWhitespace(reply);

    if (reply.size() >= 2)
    {
        char const front = reply.front();
        char const back = reply.back();
        if ((front == '"' && back == '"') || (front == '\'' && back == '\''))
        {
            reply = reply.substr(1, reply.size() - 2);
            TrimInPlace(reply);
        }
    }

    return ClampReplyLength(std::move(reply), maxChars);
}

void InitLLMProviders()
{
    ProviderConfig config;
    config.apiKey = ResolveApiKey();
    config.model = g_CloudModel;
    config.url = g_ProviderUrl;
    config.maxTokens = g_CloudMaxTokens;
    config.timeoutSeconds = g_CloudTimeoutSec;
    config.maxReplyChars = g_MaxReplyChars;
    config.debug = g_DebugEnabled;

    std::string providerName = g_CloudProvider;
    std::transform(providerName.begin(), providerName.end(), providerName.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });

    LLMProviderPtr provider;
    if (providerName == "ollama")
    {
        if (config.url.empty())
            config.url = "http://localhost:11434/api/chat";

        if (config.url.find("https://ollama.com/") == 0 && config.apiKey.empty())
        {
            LOG_INFO("server.loading",
                     "[BotMinds] Disabled: Ollama Cloud is configured without an API key. Set "
                     "BotMinds.ApiKey or BotMinds.ApiKeyEnv.");
        }
        else
        {
            provider = std::make_shared<OllamaProvider>(config);
            LOG_INFO("server.loading", "[BotMinds] Provider: Ollama (model: {}, endpoint: {}).",
                     config.model, config.url);
        }
    }
    else if (providerName == "anthropic")
    {
        if (config.apiKey.empty())
        {
            LOG_INFO("server.loading", "[BotMinds] Disabled: no API key is configured for Anthropic.");
        }
        else
        {
            provider = std::make_shared<AnthropicProvider>(config);
            LOG_INFO("server.loading", "[BotMinds] Provider: Anthropic (model: {}).", config.model);
        }
    }
    else if (providerName == "openai")
    {
        if (config.apiKey.empty())
        {
            LOG_INFO("server.loading", "[BotMinds] Disabled: no API key is configured for OpenAI.");
        }
        else
        {
            provider = std::make_shared<OpenAIProvider>(config);
            LOG_INFO("server.loading", "[BotMinds] Provider: OpenAI (model: {}).", config.model);
        }
    }
    else
    {
        LOG_ERROR("server.loading",
                  "[BotMinds] Unknown provider '{}' (expected anthropic, openai or ollama).", providerName);
    }

    std::lock_guard<std::mutex> lock(g_ProviderMutex);
    g_Provider = std::move(provider);
}

LLMProviderPtr GetProvider()
{
    std::lock_guard<std::mutex> lock(g_ProviderMutex);
    return g_Provider;
}
