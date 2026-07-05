// Apps/UltraMail/engine/UltraMailComposer.cpp
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailComposer.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>

namespace UltraMail {

namespace {

std::string Lower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return r;
}

bool HasPrefixCI(const std::string& subject, const std::string& prefix) {
    return Lower(subject).rfind(Lower(prefix), 0) == 0;
}

std::string ReplySubject(const std::string& s) {
    return HasPrefixCI(s, "re:") ? s : ("Re: " + s);
}

std::string ForwardSubject(const std::string& s) {
    if (HasPrefixCI(s, "fwd:") || HasPrefixCI(s, "fw:")) return s;
    return "Fwd: " + s;
}

// Prefix each line of `body` with "> " and add an attribution line.
std::string QuoteBody(const std::string& body, const std::string& who,
                      const std::string& date) {
    std::string out = "\n\nOn " + (date.empty() ? "an earlier date" : date) + ", "
                    + (who.empty() ? "someone" : who) + " wrote:\n";
    std::istringstream is(body);
    std::string line;
    while (std::getline(is, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out += "> " + line + "\n";
    }
    return out;
}

// Append `addr` to `dst` unless it equals `self` (case-insensitive) or is
// already present.
void AddUnique(std::vector<std::string>& dst, const std::string& addr,
               const std::string& self) {
    if (addr.empty()) return;
    std::string la = Lower(addr);
    if (la == Lower(self)) return;
    for (const auto& e : dst) if (Lower(e) == la) return;
    dst.push_back(addr);
}

std::string BuildReferences(const std::string& existing, const std::string& messageId) {
    std::string r = existing;
    if (!messageId.empty()) {
        if (!r.empty()) r += " ";
        r += messageId;
    }
    return r;
}

} // namespace

Draft Composer::NewMessage(const std::string& selfName, const std::string& selfAddr) {
    Draft d;
    d.fromName = selfName;
    d.fromAddr = selfAddr;
    return d;
}

Draft Composer::Reply(const SourceMessage& src, const std::string& selfName,
                      const std::string& selfAddr, bool replyAll) {
    Draft d;
    d.fromName = selfName;
    d.fromAddr = selfAddr;
    d.subject  = ReplySubject(src.subject);

    // Primary recipient: the original sender.
    AddUnique(d.to, src.fromAddr, selfAddr);
    if (replyAll) {
        for (const auto& a : src.to) AddUnique(d.to, a, selfAddr);
        for (const auto& a : src.cc) AddUnique(d.cc, a, selfAddr);
    }

    std::string who = src.fromName.empty() ? src.fromAddr : src.fromName;
    d.body      = QuoteBody(src.body, who, src.date);
    d.inReplyTo = src.messageId;
    d.references = BuildReferences(src.references, src.messageId);
    return d;
}

Draft Composer::Forward(const SourceMessage& src, const std::string& selfName,
                        const std::string& selfAddr) {
    Draft d;
    d.fromName = selfName;
    d.fromAddr = selfAddr;
    d.subject  = ForwardSubject(src.subject);

    std::ostringstream body;
    body << "\n\n---------- Forwarded message ----------\n"
         << "From: " << (src.fromName.empty() ? src.fromAddr
                                              : src.fromName + " <" + src.fromAddr + ">") << "\n"
         << "Date: " << src.date << "\n"
         << "Subject: " << src.subject << "\n";
    if (!src.to.empty()) {
        body << "To: ";
        for (std::size_t i = 0; i < src.to.size(); ++i) { if (i) body << ", "; body << src.to[i]; }
        body << "\n";
    }
    body << "\n" << src.body << "\n";
    d.body = body.str();

    d.attachments = src.attachments;   // forwards carry the attachments
    return d;
}

} // namespace UltraMail
