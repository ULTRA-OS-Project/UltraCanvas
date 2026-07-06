// UltraCanvas/Plugins/UltraNet/imap/ImapParse.h
// Pure, dependency-free parsers/formatters for the IMAP wire responses the
// plug-in deals with (SEARCH, LIST, STATUS, FETCH FLAGS, message headers) plus
// flag <-> IMAP-token conversion and SPECIAL-USE role detection. Kept
// header-only and free of libcurl / UltraNet-link dependencies so the logic is
// unit-testable without a live server.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <UltraNet/UltraNetPlugins.h>   // UltraNetMailFlags, folder/envelope/status structs

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
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
        char* end = nullptr;
        long v = std::strtol(body.c_str() + pos, &end, 10);
        if (end == body.c_str() + pos) break;
        if (v > 0) uids.push_back(static_cast<uint32_t>(v));
        pos = static_cast<std::size_t>(end - body.c_str());
    }
    return uids;
}

// ---- flags <-> IMAP tokens -------------------------------------------------

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

// ---- LIST ------------------------------------------------------------------

// Split a parenthesised attribute list '(\HasNoChildren \Sent)' into tokens.
inline std::vector<std::string> SplitAttributes(const std::string& parenGroup) {
    std::vector<std::string> attrs;
    std::istringstream is(parenGroup);
    std::string tok;
    while (is >> tok) if (!tok.empty()) attrs.push_back(tok);
    return attrs;
}

// SPECIAL-USE (RFC 6154) role from attributes, with an English name fallback.
inline std::string DetectFolderRole(const std::vector<std::string>& attributes,
                                    const std::string& name) {
    for (const auto& a : attributes) {
        std::string la = Lower(a);
        if (la == "\\sent")    return "sent";
        if (la == "\\drafts")  return "drafts";
        if (la == "\\junk")    return "junk";
        if (la == "\\trash")   return "trash";
        if (la == "\\archive") return "archive";
        if (la == "\\all")     return "all";
    }
    std::string ln = Lower(name);
    // Strip a leading path so "INBOX/Sent" matches "sent".
    std::size_t slash = ln.find_last_of("/.");
    std::string leaf = slash == std::string::npos ? ln : ln.substr(slash + 1);
    if (leaf == "inbox")   return "inbox";
    if (leaf == "sent" || leaf == "sent items" || leaf == "sent messages") return "sent";
    if (leaf == "drafts")  return "drafts";
    if (leaf == "junk" || leaf == "spam") return "junk";
    if (leaf == "trash" || leaf == "deleted" || leaf == "deleted items") return "trash";
    if (leaf == "archive") return "archive";
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
            std::size_t end = s.find('"', i + 1);
            if (end == std::string::npos) { std::string t = s.substr(i + 1); i = s.size(); return t; }
            std::string t = s.substr(i + 1, end - i - 1);
            i = end + 1;
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
    out.role = DetectFolderRole(out.attributes, out.name);
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
inline UltraNetMailboxStatus ParseStatusResponse(const std::string& body) {
    UltraNetMailboxStatus st;
    std::string low = Lower(body);
    auto readNum = [&](const char* key, uint32_t& dst) {
        std::size_t p = low.find(key);
        if (p == std::string::npos) return;
        p += std::string(key).size();
        while (p < body.size() && (body[p] == ' ' || body[p] == '\t')) ++p;
        char* end = nullptr;
        long v = std::strtol(body.c_str() + p, &end, 10);
        if (end != body.c_str() + p && v >= 0) dst = static_cast<uint32_t>(v);
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

} // namespace ultranet_imap
