#include "mod-bot-minds_httpclient.h"

#include "Log.h"

#include <httplib.h>

#include <exception>
#include <regex>

namespace
{
    struct ParsedUrl
    {
        bool valid = false;
        bool https = false;
        std::string host;
        int port = 0;
        std::string path;
    };

    ParsedUrl ParseUrl(std::string const& url)
    {
        static std::regex const pattern(R"(^(https?)://([^:/?#]+)(?::(\d+))?([^#]*)$)");

        std::smatch match;
        if (!std::regex_match(url, match, pattern))
            return {};

        ParsedUrl parsed;
        parsed.valid = true;
        parsed.https = match[1].str() == "https";
        parsed.host = match[2].str();
        if (match[3].matched)
        {
            unsigned long port = 0;
            try
            {
                port = std::stoul(match[3].str());
            }
            catch (std::exception const&)
            {
                return {};
            }

            if (port == 0 || port > 65535)
                return {};
            parsed.port = static_cast<int>(port);
        }
        else
        {
            parsed.port = parsed.https ? 443 : 80;
        }

        parsed.path = match[4].matched && !match[4].str().empty() ? match[4].str() : "/";
        if (!parsed.path.empty() && parsed.path.front() == '?')
            parsed.path.insert(parsed.path.begin(), '/');
        return parsed;
    }

    template <class Client>
    BotMindsHttpResult PerformPost(Client& client, ParsedUrl const& parsed, std::string const& jsonData,
                                   httplib::Headers const& headers, int timeoutSeconds, bool debug)
    {
        client.set_connection_timeout(timeoutSeconds);
        client.set_read_timeout(timeoutSeconds);
        client.set_write_timeout(timeoutSeconds);

        if (debug)
            LOG_INFO("server.loading", "[BotMinds] HTTP request to {}:{}{}", parsed.host, parsed.port, parsed.path);

        httplib::Result response = client.Post(parsed.path, headers, jsonData, "application/json");
        if (!response)
        {
            BotMindsHttpResult result;
            result.error = httplib::to_string(response.error());
            LOG_ERROR("server.loading", "[BotMinds] Request to {}:{}{} failed: {}",
                      parsed.host, parsed.port, parsed.path, result.error);
            return result;
        }

        BotMindsHttpResult result;
        result.status = response->status;
        result.body = response->body;

        if (!result.Ok())
        {
            LOG_ERROR("server.loading", "[BotMinds] {}:{}{} returned status {}: {}",
                      parsed.host, parsed.port, parsed.path, result.status, result.body);
        }
        else if (debug)
        {
            LOG_INFO("server.loading", "[BotMinds] Response received, {} bytes.", result.body.length());
        }

        return result;
    }
}

BotMindsHttpResult BotMindsHttpClient::Post(
    std::string const& url, std::string const& jsonData,
    std::vector<std::pair<std::string, std::string>> const& headers,
    int timeoutSeconds, bool debug) const
{
    ParsedUrl const parsed = ParseUrl(url);
    if (!parsed.valid)
    {
        BotMindsHttpResult result;
        result.error = "invalid URL";
        LOG_ERROR("server.loading", "[BotMinds] Invalid provider URL '{}'.", url);
        return result;
    }

    httplib::Headers httpHeaders;
    for (auto const& header : headers)
        httpHeaders.emplace(header.first, header.second);

    try
    {
        if (parsed.https)
        {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            httplib::SSLClient client(parsed.host, parsed.port);
            client.enable_server_certificate_verification(true);
            return PerformPost(client, parsed, jsonData, httpHeaders, timeoutSeconds, debug);
#else
            BotMindsHttpResult result;
            result.error = "HTTPS requested but Bot Minds was built without OpenSSL support";
            LOG_ERROR("server.loading", "[BotMinds] {}", result.error);
            return result;
#endif
        }

        httplib::Client client(parsed.host, parsed.port);
        return PerformPost(client, parsed, jsonData, httpHeaders, timeoutSeconds, debug);
    }
    catch (std::exception const& exception)
    {
        BotMindsHttpResult result;
        result.error = exception.what();
        LOG_ERROR("server.loading", "[BotMinds] HTTP client exception: {}", result.error);
        return result;
    }
}
