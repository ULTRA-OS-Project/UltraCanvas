// Plugins/UltraIM/irc/IrcMessage.cpp
// IRC lines parsed and formatted; the text inside a message made readable.
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework

#include "IrcMessage.h"

#include <cctype>
#include <cstdio>
#include <ctime>

namespace UltraIMIrc {

// ============================================================================
// LINES
// ============================================================================

bool IrcMessage::Parse(const std::string& line, IrcMessage& out) {
    out = IrcMessage();
    size_t at = 0;
    const size_t n = line.size();
    const auto skipSpaces = [&]() {
        while (at < n && line[at] == ' ') ++at;
    };
    if (at < n && line[at] == '@') {
        size_t end = line.find(' ', at);
        if (end == std::string::npos) return false;
        const std::string tags = line.substr(at + 1, end - at - 1);
        size_t start = 0;
        while (start <= tags.size()) {
            size_t semi = tags.find(';', start);
            if (semi == std::string::npos) semi = tags.size();
            const std::string item = tags.substr(start, semi - start);
            if (!item.empty()) {
                const size_t eq = item.find('=');
                if (eq == std::string::npos) out.tags[item] = std::string();
                else out.tags[item.substr(0, eq)] = IrcUnescapeTagValue(item.substr(eq + 1));
            }
            start = semi + 1;
        }
        at = end;
        skipSpaces();
    }
    if (at < n && line[at] == ':') {
        size_t end = line.find(' ', at);
        if (end == std::string::npos) return false;
        out.prefix = line.substr(at + 1, end - at - 1);
        const size_t bang = out.prefix.find('!');
        const size_t atSign = out.prefix.find('@');
        if (bang != std::string::npos) {
            out.nick = out.prefix.substr(0, bang);
            if (atSign != std::string::npos && atSign > bang) {
                out.user = out.prefix.substr(bang + 1, atSign - bang - 1);
                out.host = out.prefix.substr(atSign + 1);
            } else {
                out.user = out.prefix.substr(bang + 1);
            }
        } else if (atSign != std::string::npos) {
            out.nick = out.prefix.substr(0, atSign);
            out.host = out.prefix.substr(atSign + 1);
        } else {
            out.nick = out.prefix;
        }
        at = end;
        skipSpaces();
    }
    size_t end = line.find(' ', at);
    if (end == std::string::npos) end = n;
    out.command = line.substr(at, end - at);
    for (char& c : out.command) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (out.command.empty()) return false;
    at = end;
    while (at < n) {
        skipSpaces();
        if (at >= n) break;
        if (line[at] == ':') {
            out.params.push_back(line.substr(at + 1));
            break;
        }
        end = line.find(' ', at);
        if (end == std::string::npos) end = n;
        out.params.push_back(line.substr(at, end - at));
        at = end;
    }
    return true;
}

bool IrcMessage::IsNumeric() const {
    return command.size() == 3 && std::isdigit(static_cast<unsigned char>(command[0])) &&
           std::isdigit(static_cast<unsigned char>(command[1])) && std::isdigit(static_cast<unsigned char>(command[2]));
}

bool IrcMessage::FromServer() const {
    if (prefix.empty()) return true;
    return prefix.find('!') == std::string::npos && prefix.find('@') == std::string::npos &&
           prefix.find('.') != std::string::npos;
}

std::string IrcEscapeTagValue(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        switch (c) {
            case ';': out += "\\:"; break;
            case ' ': out += "\\s"; break;
            case '\\': out += "\\\\"; break;
            case '\r': out += "\\r"; break;
            case '\n': out += "\\n"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string IrcUnescapeTagValue(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (c != '\\') {
            out += c;
            continue;
        }
        if (i + 1 >= value.size()) break;          // a lone backslash at the end is dropped
        const char next = value[++i];
        switch (next) {
            case ':': out += ';'; break;
            case 's': out += ' '; break;
            case 'r': out += '\r'; break;
            case 'n': out += '\n'; break;
            default: out += next; break;           // "\\" and any other: the character
        }
    }
    return out;
}

std::string IrcFormatLine(const std::map<std::string, std::string>& tags, const std::string& command,
                          const std::vector<std::string>& params) {
    std::string line;
    if (!tags.empty()) {
        line += '@';
        bool first = true;
        for (const auto& [key, value] : tags) {
            if (!first) line += ';';
            first = false;
            line += key;
            if (!value.empty()) line += '=' + IrcEscapeTagValue(value);
        }
        line += ' ';
    }
    line += command;
    for (size_t i = 0; i < params.size(); ++i) {
        std::string p = params[i];
        // CR, LF and NUL end a line: never sent inside one.
        for (char& c : p) {
            if (c == '\r' || c == '\n' || c == '\0') c = ' ';
        }
        line += ' ';
        const bool last = i + 1 == params.size();
        if (last && (p.empty() || p[0] == ':' || p.find(' ') != std::string::npos)) line += ':';
        line += p;
    }
    return line;
}

// ============================================================================
// TEXT
// ============================================================================

std::string IrcStripFormatting(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    const size_t n = text.size();
    const auto isDigit = [&](size_t i) { return i < n && std::isdigit(static_cast<unsigned char>(text[i])); };
    const auto isHex = [&](size_t i) { return i < n && std::isxdigit(static_cast<unsigned char>(text[i])); };
    for (size_t i = 0; i < n; ++i) {
        const char c = text[i];
        switch (c) {
            case '\x02': case '\x0F': case '\x16': case '\x1D': case '\x1F': case '\x1E': case '\x11':
                break;
            case '\x03': {
                // \x03, \x03N, \x03NN, \x03N,N, \x03NN,NN - a comma only with a digit after it.
                size_t j = i + 1;
                if (isDigit(j)) {
                    ++j;
                    if (isDigit(j)) ++j;
                    if (j < n && text[j] == ',' && isDigit(j + 1)) {
                        j += 2;
                        if (isDigit(j)) ++j;
                    }
                }
                i = j - 1;
                break;
            }
            case '\x04': {
                size_t j = i + 1;
                size_t k = j;
                while (k < j + 6 && isHex(k)) ++k;
                if (k == j + 6) {
                    j = k;
                    if (j < n && text[j] == ',') {
                        size_t m = j + 1;
                        while (m < j + 7 && isHex(m)) ++m;
                        if (m == j + 7) j = m;
                    }
                }
                i = j - 1;
                break;
            }
            default:
                out += c;
                break;
        }
    }
    return out;
}

bool IrcIsUtf8(const std::string& bytes) {
    const size_t n = bytes.size();
    size_t i = 0;
    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(bytes[i]);
        if (c < 0x80) {
            ++i;
            continue;
        }
        size_t len = 0;
        uint32_t cp = 0;
        if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
        else return false;
        if (i + len > n) return false;
        for (size_t k = 1; k < len; ++k) {
            const unsigned char d = static_cast<unsigned char>(bytes[i + k]);
            if ((d & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (d & 0x3F);
        }
        // Over-long forms, surrogates and past U+10FFFF are not UTF-8.
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) || cp > 0x10FFFF ||
            (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
        i += len;
    }
    return true;
}

std::string IrcDecodeText(const std::string& bytes) {
    if (IrcIsUtf8(bytes)) return bytes;
    std::string out;
    out.reserve(bytes.size() + bytes.size() / 2);
    for (char ch : bytes) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c < 0x80) {
            out += ch;
        } else {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

IrcCaseMapping IrcCaseMappingFromName(const std::string& name) {
    if (name == "ascii" || name == "rfc7613" || name == "rfc8265") return IrcCaseMapping::Ascii;
    if (name == "strict-rfc1459") return IrcCaseMapping::StrictRfc1459;
    return IrcCaseMapping::Rfc1459;
}

std::string IrcFold(const std::string& name, IrcCaseMapping mapping) {
    std::string out = name;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        else if (mapping != IrcCaseMapping::Ascii && (c == '[' || c == ']' || c == '\\')) c = static_cast<char>(c + 32);
        else if (mapping == IrcCaseMapping::Rfc1459 && c == '~') c = '^';
    }
    return out;
}

std::vector<std::string> IrcSplitText(const std::string& text, size_t maxBytes) {
    std::vector<std::string> out;
    if (maxBytes < 4) maxBytes = 4;
    size_t at = 0;
    const size_t n = text.size();
    while (at < n) {
        if (n - at <= maxBytes) {
            out.push_back(text.substr(at));
            break;
        }
        // The last space within the limit (not at the very start).
        size_t cut = std::string::npos;
        for (size_t i = at + maxBytes; i > at; --i) {
            if (text[i] == ' ') {
                cut = i;
                break;
            }
        }
        if (cut != std::string::npos) {
            out.push_back(text.substr(at, cut - at));
            at = cut + 1;
            continue;
        }
        // No space: at a character's start (not a continuation byte).
        size_t end = at + maxBytes;
        while (end > at && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
        if (end == at) end = at + maxBytes;        // not UTF-8 at all: bytes
        out.push_back(text.substr(at, end - at));
        at = end;
    }
    return out;
}

int64_t IrcParseServerTime(const std::string& value) {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    int consumed = 0;
    if (std::sscanf(value.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d%n", &year, &month, &day, &hour, &minute, &second,
                    &consumed) != 6)
        return 0;
    int millis = 0;
    size_t at = static_cast<size_t>(consumed);
    if (at < value.size() && value[at] == '.') {
        ++at;
        int digits = 0;
        while (at < value.size() && std::isdigit(static_cast<unsigned char>(value[at]))) {
            if (digits < 3) millis = millis * 10 + (value[at] - '0');
            ++digits;
            ++at;
        }
        for (; digits < 3; ++digits) millis *= 10;
    }
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
#if defined(_WIN32)
    const int64_t seconds = static_cast<int64_t>(_mkgmtime(&tm));
#else
    const int64_t seconds = static_cast<int64_t>(timegm(&tm));
#endif
    if (seconds < 0) return 0;
    return seconds * 1000 + millis;
}

std::string IrcFormatServerTime(int64_t ms) {
    const std::time_t seconds = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &seconds);
#else
    gmtime_r(&seconds, &tm);
#endif
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms % 1000));
    return buffer;
}

bool IrcIsValidNick(const std::string& nick) {
    if (nick.empty() || nick.size() > 64) return false;
    const auto special = [](char c) {
        return c == '[' || c == ']' || c == '\\' || c == '`' || c == '_' || c == '^' || c == '{' || c == '|' ||
               c == '}' || c == '-';
    };
    const unsigned char first = static_cast<unsigned char>(nick[0]);
    if (std::isdigit(first) || first == '-') return false;
    for (char c : nick) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) || special(c))) return false;
    }
    return true;
}

} // namespace UltraIMIrc
