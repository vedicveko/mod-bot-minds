#ifndef MOD_BOT_MINDS_HTTPCLIENT_H
#define MOD_BOT_MINDS_HTTPCLIENT_H

#include <string>
#include <utility>
#include <vector>

struct BotMindsHttpResult
{
    int status = 0;
    std::string body;
    std::string error;

    bool Ok() const { return status >= 200 && status < 300 && error.empty(); }
};

// Minimal JSON HTTP client over cpp-httplib. Providers pass a complete URL so
// local Ollama, Ollama Cloud, and future OpenAI-compatible endpoints all use the
// same transport path.
class BotMindsHttpClient
{
public:
    BotMindsHttpResult Post(std::string const& url, std::string const& jsonData,
                            std::vector<std::pair<std::string, std::string>> const& headers,
                            int timeoutSeconds, bool debug) const;
};

#endif // MOD_BOT_MINDS_HTTPCLIENT_H
