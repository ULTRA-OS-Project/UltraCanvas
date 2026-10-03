// Apps/UltraMail/engine/UltraMailUnsubscribe.cpp
// Version: 0.2.0 - ParseMailto, shared with mailto: links in a message
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraMailUnsubscribe.h"

#include <UltraNet/UltraNetMime.h>

#include <cctype>
#include <map>
#include <vector>

namespace UltraMail {

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool StartsWithNoCase(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && Lower(s.substr(0, prefix.size())) == prefix;
}

int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Percent-decoding for mailto: parts (RFC 6068). '+' stays a '+'.
std::string PercentDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const int hi = HexValue(s[i + 1]), lo = HexValue(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>(hi * 16 + lo);
                i += 2;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

// The <...> entries of a List-Unsubscribe value, in order, with any folding
// whitespace inside them removed.
std::vector<std::string> AngleEntries(const std::string& value) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while ((pos = value.find('<', pos)) != std::string::npos) {
        const std::size_t end = value.find('>', pos + 1);
        if (end == std::string::npos) break;
        std::string entry;
        for (std::size_t i = pos + 1; i < end; ++i)
            if (!std::isspace(static_cast<unsigned char>(value[i]))) entry += value[i];
        if (!entry.empty()) out.push_back(entry);
        pos = end + 1;
    }
    return out;
}

std::string Header(const std::map<std::string, std::string>& headers,
                   const std::string& name) {
    const std::string want = Lower(name);
    for (const auto& h : headers) if (Lower(h.first) == want) return h.second;
    return "";
}

} // namespace

MailtoTarget ParseMailto(const std::string& href) {
    MailtoTarget target;
    if (!StartsWithNoCase(href, "mailto:")) return target;
    const std::string rest = href.substr(7);
    const std::size_t q = rest.find('?');
    target.address = PercentDecode(rest.substr(0, q));
    if (q == std::string::npos) return target;
    std::size_t p = q + 1;
    while (p <= rest.size()) {
        std::size_t amp = rest.find('&', p);
        if (amp == std::string::npos) amp = rest.size();
        const std::string pair = rest.substr(p, amp - p);
        const std::size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            const std::string key = Lower(pair.substr(0, eq));
            const std::string val = PercentDecode(pair.substr(eq + 1));
            if (key == "subject") target.subject = val;
            else if (key == "body") target.body = val;
        }
        p = amp + 1;
    }
    return target;
}

UnsubscribeInfo ParseListUnsubscribe(const std::string& listUnsubscribe,
                                     const std::string& listUnsubscribePost) {
    UnsubscribeInfo info;
    const bool oneClick =
        Lower(listUnsubscribePost).find("list-unsubscribe=one-click") != std::string::npos;

    for (const std::string& entry : AngleEntries(listUnsubscribe)) {
        if (StartsWithNoCase(entry, "https://") || StartsWithNoCase(entry, "http://")) {
            if (info.webUrl.empty()) info.webUrl = entry;
            // RFC 8058 requires https for the one-click POST.
            if (oneClick && info.oneClickUrl.empty() && StartsWithNoCase(entry, "https://"))
                info.oneClickUrl = entry;
        } else if (StartsWithNoCase(entry, "mailto:") && info.mailtoAddress.empty()) {
            const MailtoTarget target = ParseMailto(entry);
            info.mailtoAddress = target.address;
            info.mailtoSubject = target.subject;
            info.mailtoBody    = target.body;
        }
    }
    return info;
}

UnsubscribeInfo ReadUnsubscribe(const std::string& rawMessage) {
    UltraNetMimeMessage msg;
    if (rawMessage.empty() || !UltraNet_MimeParse(rawMessage, msg)) return {};
    return ParseListUnsubscribe(Header(msg.root.headers, "List-Unsubscribe"),
                                Header(msg.root.headers, "List-Unsubscribe-Post"));
}

} // namespace UltraMail
