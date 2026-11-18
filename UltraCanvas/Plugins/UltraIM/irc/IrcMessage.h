// Plugins/UltraIM/irc/IrcMessage.h
// IRC lines as the IRC provider reads and writes them: the IRCv3 message
// tags, the prefix (nick!user@host or a server's name), the command and its
// parameters (RFC 1459 / 2812, modern.ircdocs.horse); the text inside a
// message - mIRC formatting codes taken out, bytes that are not UTF-8 read
// as Latin-1, CTCP - the server's case folding (CASEMAPPING), server-time,
// and a long text split into pieces that each fit one 512-byte line.
// Pure functions: no network, no state.
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace UltraIMIrc {

// One line from (or to) the server, its CR LF taken off.
struct IrcMessage {
    std::map<std::string, std::string> tags;    // unescaped values; "" for a tag without one
    std::string prefix;                         // "nick!user@host" or "irc.example.org" ("" = none)
    std::string nick;                           // the prefix's nick (or the server's name)
    std::string user;
    std::string host;
    std::string command;                        // upper case: "PRIVMSG", "001"
    std::vector<std::string> params;            // the trailing one included, as bytes

    // False for a line with no command.
    static bool Parse(const std::string& line, IrcMessage& out);

    std::string Param(size_t i) const { return i < params.size() ? params[i] : std::string(); }
    std::string Tag(const std::string& key) const {
        auto it = tags.find(key);
        return it != tags.end() ? it->second : std::string();
    }
    bool HasTag(const std::string& key) const { return tags.count(key) != 0; }
    // A numeric reply ("001", "433").
    bool IsNumeric() const;
    // The prefix names a server, not a person (no "!" and a dot in it, or none).
    bool FromServer() const;
};

// A line to send: the tags (values escaped here), the command, the
// parameters - the last one with ':' when it needs it. No CR LF.
std::string IrcFormatLine(const std::map<std::string, std::string>& tags, const std::string& command,
                          const std::vector<std::string>& params);

std::string IrcEscapeTagValue(const std::string& value);
std::string IrcUnescapeTagValue(const std::string& value);

// The text with the mIRC formatting codes taken out: bold \x02, colour
// \x03NN[,NN], hex colour \x04RRGGBB[,RRGGBB], reset \x0F, reverse \x16,
// italic \x1D, underline \x1F, strike-through \x1E, monospace \x11.
std::string IrcStripFormatting(const std::string& text);

// The bytes as UTF-8: as they are when they are UTF-8, else read as Latin-1
// (what most IRC clients that do not send UTF-8 send).
std::string IrcDecodeText(const std::string& bytes);
bool IrcIsUtf8(const std::string& bytes);

// How the server compares nicks and channel names (005 CASEMAPPING).
enum class IrcCaseMapping { Ascii, Rfc1459, StrictRfc1459 };
IrcCaseMapping IrcCaseMappingFromName(const std::string& name);
std::string IrcFold(const std::string& name, IrcCaseMapping mapping);

// A text cut into pieces of at most maxBytes bytes each: at the last space
// that fits (the space dropped), else at the last UTF-8 character that fits -
// never inside one. Joined back with single spaces where a space was cut,
// the pieces are the text.
std::vector<std::string> IrcSplitText(const std::string& text, size_t maxBytes);

// server-time ("2026-11-17T10:20:30.123Z") in milliseconds since the epoch;
// 0 when it is not one.
int64_t IrcParseServerTime(const std::string& value);
std::string IrcFormatServerTime(int64_t ms);

// A nick as IRC allows one (letters, digits, []\`_^{|}- ; not starting with
// a digit or '-').
bool IrcIsValidNick(const std::string& nick);

} // namespace UltraIMIrc
