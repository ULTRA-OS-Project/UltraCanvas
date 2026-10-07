// UltraCanvas/Plugins/UltraNet/imap/ImapParse.h
// Pure, dependency-free parsers/formatters for the IMAP wire responses the
// plug-in deals with (SEARCH, LIST, STATUS, FETCH FLAGS, message headers) plus
// flag <-> IMAP-token conversion and SPECIAL-USE role detection. Kept
// header-only and free of libcurl / UltraNet-link dependencies so the logic is
// unit-testable without a live server.
// Version: 0.7.0 - a quoted LIST name or delimiter is unescaped (\" and \\);
//                  ImapResponse::AsLine (a response with its literals as quoted strings, for
//                  the LIST / STATUS parsers); RFC822.SIZE in ParseFetchResponse
// Version: 0.6.0 - ImapResponseReader (responses with their literals, read as they
//                  arrive), ParseFetchResponse, UidSetString: what the batched
//                  header and body fetches read
// Version: 0.5.0 - DetectFolderRole: by the last level after the server's own
//                  separator, German names, a migrated "INBOX^" prefix; only
//                  INBOX itself is the inbox
// Version: 0.4.0 - numbers are read as unsigned 32-bit values on every platform
//                  (ParseImapNumber): strtol's `long` is 32 bits on Windows, so
//                  a UIDVALIDITY, UIDNEXT or UID above 2147483647 read there as
//                  2147483647
// Version: 0.3.0 - UidExpungeCommand
// Version: 0.2.0 - RawHeaderValue, SearchByMessageIdCommand (APPEND's flags)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <UltraNet/UltraNetPlugins.h>   // UltraNetMailFlags, folder/envelope/status structs

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ultranet_imap {

// ---- small string helpers --------------------------------------------------

inline std::string TrimWs(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b-1] == ' ' || s[b-1] == '\t' || s[b-1] == '\r' || s[b-1] == '\n')) --b;
    return s.substr(a, b - a);
}

inline std::string Lower(const std::string& s) {
    std::string r; r.reserve(s.size());
    for (char c : s) r.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return r;
}

// A protocol number (UID, UIDVALIDITY, UIDNEXT, a count) at `p`: an IMAP
// nz-number is unsigned 32-bit (RFC 3501), up to 4294967295. Read as
// unsigned long long, never `long`, which is 32 bits on Windows and stops at
// 2147483647 there - one value for every larger UIDVALIDITY, so a renumbered
// mailbox looked unchanged on Windows only. False when no digit is at `p` or
// the value does not fit; `end` receives the position after the digits.
inline bool ParseImapNumber(const std::string& s, std::size_t p, uint32_t& out,
                            std::size_t* end = nullptr) {
    if (p >= s.size() || s[p] < '0' || s[p] > '9') return false;
    unsigned long long v = 0;
    std::size_t i = p;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        v = v * 10 + static_cast<unsigned long long>(s[i] - '0');
        if (v > 0xFFFFFFFFull) return false;
        ++i;
    }
    if (end) *end = i;
    out = static_cast<uint32_t>(v);
    return true;
}

// ---- SEARCH ----------------------------------------------------------------

// Parse "* SEARCH 1 2 3 4 5" -> {1,2,3,4,5}. Tolerates multi-line responses.
inline std::vector<uint32_t> ParseSearchUids(const std::string& body) {
    std::vector<uint32_t> uids;
    std::size_t pos = body.find("SEARCH");
    if (pos == std::string::npos) return uids;
    pos += 6;
    while (pos < body.size()) {
        while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\t')) ++pos;
        if (pos >= body.size() || body[pos] == '\r' || body[pos] == '\n') break;
        uint32_t v = 0;
        std::size_t end = pos;
        if (!ParseImapNumber(body, pos, v, &end)) break;
        if (v > 0) uids.push_back(v);
        pos = end;
    }
    return uids;
}

// The value of the first `name` header of a raw RFC 5322 message, unfolded
// and trimmed ("" when it has none). Only the header block is looked at.
inline std::string RawHeaderValue(const std::string& raw, const std::string& name) {
    auto lower = [](std::string t) {
        for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return t;
    };
    const std::string wanted = lower(name) + ":";
    std::size_t lineStart = 0;
    while (lineStart < raw.size()) {
        std::size_t lineEnd = raw.find('\n', lineStart);
        if (lineEnd == std::string::npos) lineEnd = raw.size();
        std::string line = raw.substr(lineStart, lineEnd - lineStart);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;   // end of the header block
        if (lower(line.substr(0, wanted.size())) == wanted) {
            std::string value = line.substr(wanted.size());
            // Folded continuation lines start with whitespace.
            std::size_t next = lineEnd + 1;
            while (next < raw.size() && (raw[next] == ' ' || raw[next] == '\t')) {
                std::size_t end = raw.find('\n', next);
                if (end == std::string::npos) end = raw.size();
                std::string more = raw.substr(next, end - next);
                if (!more.empty() && more.back() == '\r') more.pop_back();
                value += more;
                next = end + 1;
            }
            std::size_t b = value.find_first_not_of(" \t");
            std::size_t e = value.find_last_not_of(" \t");
            return b == std::string::npos ? std::string() : value.substr(b, e - b + 1);
        }
        lineStart = lineEnd + 1;
    }
    return {};
}

// "UID SEARCH HEADER Message-ID \"<id@host>\"" - how a message just uploaded
// with APPEND is found again (libcurl does not report its UID), so its flags
// can be set. The ID is quoted with \ and " escaped.
inline std::string SearchByMessageIdCommand(const std::string& messageId) {
    std::string quoted;
    for (char c : messageId) {
        if (c == '"' || c == '\\') quoted += '\\';
        if (c != '\r' && c != '\n') quoted += c;
    }
    return "UID SEARCH HEADER Message-ID \"" + quoted + "\"";
}

// "UID EXPUNGE <uid>" (RFC 4315): removes that one message, if it is flagged
// \Deleted - a plain EXPUNGE would remove every message flagged so.
inline std::string UidExpungeCommand(uint32_t uid) {
    return "UID EXPUNGE " + std::to_string(uid);
}

// ---- flags <-> IMAP tokens -------------------------------------------------

// "UID FETCH <uid> (FLAGS)" - one message's flags.
inline std::string UidFetchFlagsCommand(uint32_t uid) {
    return "UID FETCH " + std::to_string(uid) + " (FLAGS)";
}

// "UID STORE <uid> -FLAGS.SILENT (\Seen)" - the message is unread again, and
// the server sends no FETCH back for it.
inline std::string UidMarkUnreadCommand(uint32_t uid) {
    return "UID STORE " + std::to_string(uid) + " -FLAGS.SILENT (\\Seen)";
}

// Whether a "UID FETCH n (FLAGS)" response reported the flags at all: an
// unanswered fetch must not read as "no flags", i.e. unread.
inline bool HasFetchFlags(const std::string& fetchResponse) {
    return Lower(fetchResponse).find("flags") != std::string::npos;
}

// UltraNetMailFlags -> "\Seen \Answered" (space-separated IMAP flag tokens).
inline std::string FlagsToImapString(UltraNetMailFlags flags) {
    std::string out;
    auto add = [&](UltraNetMailFlags f, const char* tok) {
        if (UltraNetHasFlag(flags, f)) { if (!out.empty()) out += ' '; out += tok; }
    };
    add(UltraNetMailFlags::Seen,     "\\Seen");
    add(UltraNetMailFlags::Answered, "\\Answered");
    add(UltraNetMailFlags::Flagged,  "\\Flagged");
    add(UltraNetMailFlags::Deleted,  "\\Deleted");
    add(UltraNetMailFlags::Draft,    "\\Draft");
    return out;
}

// "\Seen \Answered" (case-insensitive) -> UltraNetMailFlags bitfield.
inline UltraNetMailFlags ImapStringToFlags(const std::string& tokens) {
    UltraNetMailFlags flags = UltraNetMailFlags::None;
    std::string low = Lower(tokens);
    if (low.find("\\seen")     != std::string::npos) flags |= UltraNetMailFlags::Seen;
    if (low.find("\\answered") != std::string::npos) flags |= UltraNetMailFlags::Answered;
    if (low.find("\\flagged")  != std::string::npos) flags |= UltraNetMailFlags::Flagged;
    if (low.find("\\deleted")  != std::string::npos) flags |= UltraNetMailFlags::Deleted;
    if (low.find("\\draft")    != std::string::npos) flags |= UltraNetMailFlags::Draft;
    if (low.find("\\recent")   != std::string::npos) flags |= UltraNetMailFlags::Recent;
    return flags;
}

// Extract the flags from a FETCH response fragment containing "FLAGS (...)".
inline UltraNetMailFlags ParseFetchFlags(const std::string& fetchLine) {
    std::string low = Lower(fetchLine);
    std::size_t f = low.find("flags");
    if (f == std::string::npos) return UltraNetMailFlags::None;
    std::size_t open = fetchLine.find('(', f);
    if (open == std::string::npos) return UltraNetMailFlags::None;
    std::size_t close = fetchLine.find(')', open);
    if (close == std::string::npos) return UltraNetMailFlags::None;
    return ImapStringToFlags(fetchLine.substr(open + 1, close - open - 1));
}

// Parse a whole "UID FETCH 1:* (FLAGS)" response into (uid, flags) pairs. Each
// data line looks like '* 12 FETCH (UID 100 FLAGS (\Seen \Flagged))'. A line
// without a UID token is skipped (there is nothing to reconcile it against).
inline std::vector<std::pair<uint32_t, UltraNetMailFlags>>
ParseAllFlags(const std::string& body) {
    std::vector<std::pair<uint32_t, UltraNetMailFlags>> out;
    std::istringstream is(body);
    std::string line;
    while (std::getline(is, line)) {
        std::string low = Lower(line);
        if (low.find("fetch") == std::string::npos) continue;
        // The UID token: "UID <n>" (case-insensitive), independent of the FLAGS
        // group (Gmail returns them in either order).
        std::size_t up = low.find("uid");
        if (up == std::string::npos) continue;
        std::size_t np = up + 3;
        while (np < line.size() && (line[np] == ' ' || line[np] == '\t')) ++np;
        uint32_t uid = 0;
        if (!ParseImapNumber(line, np, uid) || uid == 0) continue;
        out.emplace_back(uid, ParseFetchFlags(line));
    }
    return out;
}

// ---- LIST ------------------------------------------------------------------

// Split a parenthesised attribute list '(\HasNoChildren \Sent)' into tokens.
inline std::vector<std::string> SplitAttributes(const std::string& parenGroup) {
    std::vector<std::string> attrs;
    std::istringstream is(parenGroup);
    std::string tok;
    while (is >> tok) if (!tok.empty()) attrs.push_back(tok);
    return attrs;
}

// SPECIAL-USE (RFC 6154) role from attributes, else from the folder's name.
// The name is matched by its last level (after `delimiter`, the separator the
// server listed; '/' or '.' when not given), in the English and German names
// mail servers use ("Sent Items", "Gesendete Objekte", "Papierkorb", …) - names
// outside ASCII as their modified UTF-7 wire form. A leading "INBOX^" in that
// level is left out: it is how a folder came across from a server with another
// separator (Courier's "INBOX.Sent" became "INBOX.INBOX^Sent"). Only the folder
// named INBOX itself is the inbox - not a sub-folder that happens to be called so.
inline std::string DetectFolderRole(const std::vector<std::string>& attributes,
                                    const std::string& name,
                                    const std::string& delimiter = "") {
    for (const auto& a : attributes) {
        std::string la = Lower(a);
        if (la == "\\sent")    return "sent";
        if (la == "\\drafts")  return "drafts";
        if (la == "\\junk")    return "junk";
        if (la == "\\trash")   return "trash";
        if (la == "\\archive") return "archive";
        if (la == "\\all")     return "all";
    }
    const std::string ln = Lower(name);
    if (ln == "inbox") return "inbox";
    const std::size_t cut = delimiter.empty() ? ln.find_last_of("/.")
                                              : ln.rfind(Lower(delimiter));
    std::string leaf = cut == std::string::npos
        ? ln : ln.substr(cut + (delimiter.empty() ? 1 : delimiter.size()));
    if (leaf.rfind("inbox^", 0) == 0) leaf = leaf.substr(6);
    auto any = [&leaf](std::initializer_list<const char*> names) {
        for (const char* n : names) if (leaf == n) return true;
        return false;
    };
    if (any({"sent", "sent items", "sent messages", "sent mail", "sent-mail",
             "gesendet", "gesendete objekte", "gesendete elemente",
             "gesendete nachrichten"}))
        return "sent";
    if (any({"drafts", "draft", "entw&apw-rfe", "entwurf"}))
        return "drafts";
    if (any({"junk", "spam", "junk e-mail", "junk email", "junk-e-mail", "bulk mail",
             "spamverdacht"}))
        return "junk";
    if (any({"trash", "deleted", "deleted items", "deleted messages", "bin",
             "papierkorb", "gel&apy-schte objekte", "gel&apy-schte elemente"}))
        return "trash";
    if (any({"archive", "archives", "archiv"}))
        return "archive";
    return "";
}

// Parse one LIST/LSUB line: '* LIST (\HasNoChildren) "/" "INBOX/Sent"'.
// Returns false if the line is not a LIST data line.
inline bool ParseListLine(const std::string& line, UltraNetMailFolder& out) {
    std::string low = Lower(line);
    if (low.find(" list ") == std::string::npos &&
        low.rfind("* list", 0) != 0 &&
        low.find("lsub") == std::string::npos) {
        // accept "* LIST" / "* LSUB" only
    }
    std::size_t l = low.find("list");
    if (l == std::string::npos) l = low.find("lsub");
    if (l == std::string::npos) return false;

    std::size_t open = line.find('(', l);
    std::size_t close = line.find(')', open == std::string::npos ? l : open);
    if (open == std::string::npos || close == std::string::npos) return false;
    out.attributes = SplitAttributes(line.substr(open + 1, close - open - 1));

    // After the attribute group: <delimiter> <name>, each either quoted or NIL.
    std::string rest = TrimWs(line.substr(close + 1));

    auto readToken = [](const std::string& s, std::size_t& i) -> std::string {
        while (i < s.size() && s[i] == ' ') ++i;
        if (i >= s.size()) return "";
        if (s[i] == '"') {
            // A quoted string: \" and \\ stand for " and \ (RFC 3501 4.3) -
            // a folder named Say "hi", or the delimiter \ sent as "\\".
            std::string t;
            for (++i; i < s.size() && s[i] != '"'; ++i) {
                if (s[i] == '\\' && i + 1 < s.size()) ++i;
                t += s[i];
            }
            if (i < s.size()) ++i;   // the closing quote
            return t;
        }
        std::size_t start = i;
        while (i < s.size() && s[i] != ' ') ++i;
        return s.substr(start, i - start);
    };

    std::size_t i = 0;
    std::string delim = readToken(rest, i);
    std::string name  = readToken(rest, i);
    if (name.empty()) return false;

    out.delimiter = (Lower(delim) == "nil") ? "" : delim;
    out.name = name;
    out.selectable = true;
    for (const auto& a : out.attributes)
        if (Lower(a) == "\\noselect") out.selectable = false;
    out.role = DetectFolderRole(out.attributes, out.name, out.delimiter);
    return true;
}

// Parse a full LIST response into folders.
inline std::vector<UltraNetMailFolder> ParseListResponse(const std::string& body) {
    std::vector<UltraNetMailFolder> folders;
    std::istringstream is(body);
    std::string line;
    while (std::getline(is, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string low = Lower(TrimWs(line));
        if (low.rfind("* list", 0) != 0 && low.rfind("* lsub", 0) != 0) continue;
        UltraNetMailFolder f;
        if (ParseListLine(line, f)) folders.push_back(std::move(f));
    }
    return folders;
}

// ---- STATUS ----------------------------------------------------------------

// Parse '* STATUS "INBOX" (MESSAGES 3 RECENT 1 UIDNEXT 12 UIDVALIDITY 7 UNSEEN 2)'.
// The items are looked for after the last '(' only, so a mailbox whose name
// holds one of the words ("Recent messages") cannot hide the real value.
inline UltraNetMailboxStatus ParseStatusResponse(const std::string& body) {
    UltraNetMailboxStatus st;
    std::string low = Lower(body);
    const std::size_t items = low.rfind('(');
    auto readNum = [&](const char* key, uint32_t& dst) {
        std::size_t p = low.find(key, items == std::string::npos ? 0 : items);
        if (p == std::string::npos) return;
        p += std::string(key).size();
        while (p < body.size() && (body[p] == ' ' || body[p] == '\t')) ++p;
        uint32_t v = 0;
        if (ParseImapNumber(body, p, v)) dst = v;
    };
    readNum("messages",    st.messages);
    readNum("recent",      st.recent);
    readNum("uidnext",     st.uidNext);
    readNum("uidvalidity", st.uidValidity);
    readNum("unseen",      st.unseen);
    return st;
}

// ---- message headers -------------------------------------------------------

inline void SplitAddrList(const std::string& src, std::vector<std::string>& out) {
    std::size_t i = 0;
    while (i < src.size()) {
        std::size_t comma = src.find(',', i);
        if (comma == std::string::npos) comma = src.size();
        std::string a = TrimWs(src.substr(i, comma - i));
        if (!a.empty()) out.push_back(a);
        i = comma + 1;
    }
}

// Unfold + parse an RFC 5322 header block into an envelope's header fields.
// `headerBlock` is just the header text (no body).
inline void ParseEnvelopeHeaders(const std::string& headerBlock,
                                 UltraNetMailEnvelope& env) {
    std::string unfolded;
    {
        std::istringstream is(headerBlock);
        std::string line;
        while (std::getline(is, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty() && (line.front() == ' ' || line.front() == '\t') &&
                !unfolded.empty()) {
                unfolded += ' ';
                unfolded += TrimWs(line);
            } else {
                if (!unfolded.empty()) unfolded += '\n';
                unfolded += line;
            }
        }
    }
    std::istringstream iss(unfolded);
    std::string line;
    while (std::getline(iss, line)) {
        std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = Lower(TrimWs(line.substr(0, colon)));
        std::string value = TrimWs(line.substr(colon + 1));
        if (name == "from")            env.from = value;
        else if (name == "to")         SplitAddrList(value, env.to);
        else if (name == "cc")         SplitAddrList(value, env.cc);
        else if (name == "subject")    env.subject = value;
        else if (name == "date")       env.date = value;
        else if (name == "message-id") env.messageId = value;
        else if (name == "in-reply-to") env.inReplyTo = value;
    }
}

// ---- responses with literals ------------------------------------------------

// One response from the server as it came off the wire. A string the server
// sends as a literal - "{123}" at the end of a line, then exactly that many
// bytes - is kept apart: `literals[i]` is the i-th literal, `segments[i]` the
// text before it (ending in its "{123}"), and the last segment the text after
// the last literal. A response without literals is one segment, its line.
struct ImapResponse {
    std::vector<std::string> segments;
    std::vector<std::string> literals;

    // The response's text with its literals left out.
    std::string Text() const {
        std::string out;
        for (const auto& s : segments) out += s;
        return out;
    }
    // The response as one line, each literal written in as a quoted string -
    // what the line-based parsers (LIST, STATUS) read. Line breaks inside a
    // literal become spaces; a folder name or a count never holds one.
    std::string AsLine() const {
        std::string out;
        for (std::size_t i = 0; i < segments.size(); ++i) {
            std::string seg = segments[i];
            if (i < literals.size()) {
                const std::size_t open = seg.rfind('{');
                if (open != std::string::npos) seg.erase(open);
                if (open != std::string::npos && open > 0 && seg.back() == '~') seg.pop_back();
                out += seg;
                out += '"';
                for (char c : literals[i]) {
                    if (c == '\r' || c == '\n') { out += ' '; continue; }
                    if (c == '\\' || c == '"') out += '\\';
                    out += c;
                }
                out += '"';
            } else {
                out += seg;
            }
        }
        return out;
    }
    // Whether this is the tagged completion of command `tag` ("U3 OK ...").
    bool IsTagged(const std::string& tag) const {
        return !segments.empty() && segments.front().compare(0, tag.size() + 1, tag + " ") == 0;
    }
    // "OK" / "NO" / "BAD" of a tagged response (upper-cased), "" otherwise.
    std::string Status() const {
        if (segments.empty()) return {};
        const std::string& line = segments.front();
        const std::size_t sp = line.find(' ');
        if (sp == std::string::npos) return {};
        std::size_t end = line.find(' ', sp + 1);
        if (end == std::string::npos) end = line.size();
        std::string st = line.substr(sp + 1, end - sp - 1);
        for (auto& c : st) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return st;
    }
};

// The size of the literal a line announces at its end - "{123}", "{123+}" or a
// literal8's "~{123}" - or false when the line ends without one.
inline bool LiteralAtLineEnd(const std::string& line, std::size_t& size) {
    if (line.size() < 3 || line.back() != '}') return false;
    std::size_t open = line.rfind('{');
    if (open == std::string::npos) return false;
    std::size_t endDigits = line.size() - 1;
    if (endDigits > open + 1 && line[endDigits - 1] == '+') --endDigits;
    if (endDigits == open + 1) return false;
    unsigned long long v = 0;
    for (std::size_t i = open + 1; i < endDigits; ++i) {
        if (line[i] < '0' || line[i] > '9') return false;
        v = v * 10 + static_cast<unsigned long long>(line[i] - '0');
        if (v > 0xFFFFFFFFull) return false;
    }
    size = static_cast<std::size_t>(v);
    return true;
}

// Cuts the byte stream of a connection into responses: Feed what was read,
// then take each complete response with Next. A literal's bytes may contain
// anything, line breaks included, and may arrive in any number of pieces.
class ImapResponseReader {
public:
    void Feed(const char* data, std::size_t n) { buf_.append(data, n); }

    bool Next(ImapResponse& out) {
        for (;;) {
            if (literalLeft_ > 0 || inLiteral_) {
                if (buf_.size() - pos_ < literalLeft_) return false;
                partial_.literals.push_back(buf_.substr(pos_, literalLeft_));
                pos_ += literalLeft_;
                literalLeft_ = 0;
                inLiteral_ = false;
                continue;
            }
            const std::size_t eol = buf_.find("\r\n", pos_);
            if (eol == std::string::npos) { Compact(); return false; }
            std::string line = buf_.substr(pos_, eol - pos_);
            pos_ = eol + 2;
            std::size_t size = 0;
            const bool literal = LiteralAtLineEnd(line, size);
            partial_.segments.push_back(std::move(line));
            if (literal) {
                literalLeft_ = size;
                inLiteral_ = true;   // a {0} literal still has to be taken
                continue;
            }
            out = std::move(partial_);
            partial_ = ImapResponse{};
            Compact();
            return true;
        }
    }

    // Bytes held that no complete response has used yet.
    std::size_t Pending() const { return buf_.size() - pos_; }

private:
    void Compact() {
        if (pos_ > 0 && (pos_ >= buf_.size() || pos_ > (1u << 16))) {
            buf_.erase(0, pos_);
            pos_ = 0;
        }
    }
    std::string  buf_;
    std::size_t  pos_ = 0;
    std::size_t  literalLeft_ = 0;
    bool         inLiteral_ = false;
    ImapResponse partial_;
};

// ---- FETCH -------------------------------------------------------------------

// What one "* n FETCH (...)" response says about a message: its UID, its flags
// when they were asked for, and the sections that came as literals, keyed by
// name without any "<origin>" ("BODY[HEADER]", "BODY[]").
struct ImapFetchItem {
    uint32_t uid = 0;
    bool hasFlags = false;
    UltraNetMailFlags flags = UltraNetMailFlags::None;
    bool hasSize = false;
    uint32_t size = 0;          // RFC822.SIZE, when asked for
    std::map<std::string, std::string> sections;
};

// Read a FETCH response. False for any other response, and for a FETCH
// without a UID (an unsolicited flag change has nothing to file it under).
inline bool ParseFetchResponse(const ImapResponse& r, ImapFetchItem& out) {
    out = ImapFetchItem{};
    if (r.segments.empty()) return false;
    // "* <n> FETCH (" at the start.
    const std::string& first = r.segments.front();
    if (first.size() < 2 || first[0] != '*' || first[1] != ' ') return false;
    std::size_t p = 2, afterNum = 0;
    uint32_t seq = 0;
    if (!ParseImapNumber(first, p, seq, &afterNum)) return false;
    if (Lower(first.substr(afterNum, 7)) != " fetch ") return false;

    // The items outside the literals; a placeholder keeps a literal's place so
    // nothing reads across it.
    std::string text;
    for (std::size_t i = 0; i < r.segments.size(); ++i) {
        text += r.segments[i];
        if (i < r.literals.size()) text += " \x01 ";
    }
    const std::string low = Lower(text);
    // "UID <n>" as an item of its own (not the tail of another word).
    for (std::size_t at = low.find("uid"); at != std::string::npos; at = low.find("uid", at + 3)) {
        if (at > 0 && low[at - 1] != ' ' && low[at - 1] != '(') continue;
        std::size_t np = at + 3;
        if (np >= low.size() || low[np] != ' ') continue;
        while (np < low.size() && low[np] == ' ') ++np;
        if (ParseImapNumber(text, np, out.uid)) break;
    }
    if (out.uid == 0) return false;
    if (const std::size_t at = low.find("rfc822.size "); at != std::string::npos) {
        std::size_t np = at + 12;
        while (np < low.size() && low[np] == ' ') ++np;
        out.hasSize = ParseImapNumber(text, np, out.size);
    }
    if (low.find("flags (") != std::string::npos) {
        out.hasFlags = true;
        out.flags = ParseFetchFlags(text.substr(low.find("flags (")));
    }
    // Each literal is the value of the section named just before it.
    for (std::size_t i = 0; i < r.literals.size() && i < r.segments.size(); ++i) {
        const std::string& seg = r.segments[i];
        const std::size_t name = Lower(seg).rfind("body[");
        if (name == std::string::npos) continue;
        const std::size_t close = seg.find(']', name);
        if (close == std::string::npos) continue;
        std::string key = seg.substr(name, close - name + 1);
        for (auto& c : key) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        out.sections[key] = r.literals[i];
    }
    return true;
}

// A UID set for a command: runs of consecutive UIDs as "first:last", the rest
// one by one - "3:7,9,12:13". Order and repeats in `uids` do not matter.
inline std::string UidSetString(std::vector<uint32_t> uids) {
    std::sort(uids.begin(), uids.end());
    uids.erase(std::unique(uids.begin(), uids.end()), uids.end());
    std::string out;
    for (std::size_t i = 0; i < uids.size();) {
        std::size_t j = i;
        while (j + 1 < uids.size() && uids[j + 1] == uids[j] + 1) ++j;
        if (!out.empty()) out += ',';
        out += std::to_string(uids[i]);
        if (j > i) out += ':' + std::to_string(uids[j]);
        i = j + 1;
    }
    return out;
}

} // namespace ultranet_imap
