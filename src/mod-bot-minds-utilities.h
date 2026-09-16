#ifndef MOD_BOT_MINDS_UTILS_H
#define MOD_BOT_MINDS_UTILS_H

#include "Log.h"

#include <fmt/format.h>
#include <sstream>
#include <string>
#include <vector>

template<typename... Args>
inline std::string SafeFormat(std::string const& templ, Args&&... args)
{
    try
    {
        return fmt::vformat(templ, fmt::make_format_args(args...));
    }
    catch (fmt::format_error const& exception)
    {
        LOG_ERROR("server.loading", "[BotMinds] Format error: {} | Template: {}", exception.what(), templ);
        return "[Format Error]";
    }
}

inline std::vector<std::string> SplitString(std::string const& text, char delimiter)
{
    std::vector<std::string> tokens;
    std::stringstream stream(text);
    std::string token;
    while (std::getline(stream, token, delimiter))
    {
        size_t const start = token.find_first_not_of(" \t");
        size_t const end = token.find_last_not_of(" \t");
        if (start != std::string::npos && end != std::string::npos)
            tokens.push_back(token.substr(start, end - start + 1));
    }
    return tokens;
}

// Remove invalid UTF-8 bytes without changing valid sequences.
inline std::string SanitizeUTF8(std::string const& text)
{
    std::string result;
    result.reserve(text.size());

    for (size_t index = 0; index < text.size();)
    {
        unsigned char const character = static_cast<unsigned char>(text[index]);

        if (character <= 0x7F)
        {
            result.push_back(text[index]);
            ++index;
        }
        else if ((character & 0xE0) == 0xC0)
        {
            if (index + 1 < text.size()
                && (static_cast<unsigned char>(text[index + 1]) & 0xC0) == 0x80)
            {
                result.push_back(text[index]);
                result.push_back(text[index + 1]);
                index += 2;
            }
            else
            {
                result.push_back(' ');
                ++index;
            }
        }
        else if ((character & 0xF0) == 0xE0)
        {
            if (index + 2 < text.size()
                && (static_cast<unsigned char>(text[index + 1]) & 0xC0) == 0x80
                && (static_cast<unsigned char>(text[index + 2]) & 0xC0) == 0x80)
            {
                result.push_back(text[index]);
                result.push_back(text[index + 1]);
                result.push_back(text[index + 2]);
                index += 3;
            }
            else
            {
                result.push_back(' ');
                ++index;
            }
        }
        else if ((character & 0xF8) == 0xF0)
        {
            if (index + 3 < text.size()
                && (static_cast<unsigned char>(text[index + 1]) & 0xC0) == 0x80
                && (static_cast<unsigned char>(text[index + 2]) & 0xC0) == 0x80
                && (static_cast<unsigned char>(text[index + 3]) & 0xC0) == 0x80)
            {
                result.push_back(text[index]);
                result.push_back(text[index + 1]);
                result.push_back(text[index + 2]);
                result.push_back(text[index + 3]);
                index += 4;
            }
            else
            {
                result.push_back(' ');
                ++index;
            }
        }
        else
        {
            result.push_back(' ');
            ++index;
        }
    }

    return result;
}

#endif // MOD_BOT_MINDS_UTILS_H
