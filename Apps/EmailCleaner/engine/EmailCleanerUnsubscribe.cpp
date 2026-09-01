// Apps/EmailCleaner/engine/EmailCleanerUnsubscribe.cpp
// Version: 0.2.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "EmailCleanerUnsubscribe.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace EmailCleaner {

namespace {

std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool StartsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && Lower(s.substr(0, prefix.size())) == prefix;
}

// Split the header into its individual URIs. The grammar is angle-bracketed
// entries separated by commas, but a comma can also appear *inside* a mailto
// query, so brackets win over commas wherever they are present.
std::vector<std::string> SplitEntries(const std::string& headerValue) {
    std::vector<std::string> entries;
    std::string current;
    bool inBrackets = false;
    bool sawBrackets = false;

    for (char c : headerValue) {
        if (c == '<') {
            inBrackets = true;
            sawBrackets = true;
            current.clear();
            continue;
        }
        if (c == '>') {
            inBrackets = false;
            const std::string entry = Trim(current);
            if (!entry.empty()) entries.push_back(entry);
            current.clear();
            continue;
        }
        if (c == ',' && !inBrackets) {
            // Only meaningful as a separator outside brackets.
            const std::string entry = Trim(current);
            if (!sawBrackets && !entry.empty()) entries.push_back(entry);
            current.clear();
            continue;
        }
        current.push_back(c);
    }
    // A trailing unbracketed entry (a header that omitted the brackets).
    const std::string tail = Trim(current);
    if (!sawBrackets && !tail.empty()) entries.push_back(tail);
    return entries;
}

} // namespace

std::string ToString(UnsubscribeMethod method) {
    switch (method) {
        case UnsubscribeMethod::OneClickPost: return "one-click";
        case UnsubscribeMethod::MailTo:       return "email";
        case UnsubscribeMethod::HttpLink:     return "link";
        case UnsubscribeMethod::None:         break;
    }
    return "none";
}

std::string ToString(UnsubscribeAdvice advice) {
    switch (advice) {
        case UnsubscribeAdvice::NoOffer:      return "no-offer";
        case UnsubscribeAdvice::NeedsBrowser: return "needs-browser";
        case UnsubscribeAdvice::RefuseSpam:   return "refuse-spam";
        case UnsubscribeAdvice::Recommended:  break;
    }
    return "recommended";
}

std::string DescribeAdvice(UnsubscribeAdvice advice, MessageCategory category) {
    switch (advice) {
        case UnsubscribeAdvice::Recommended:
            return "This sender offers a standard unsubscribe. Taking it is the "
                   "clean way to stop the mail.";
        case UnsubscribeAdvice::NeedsBrowser:
            return "The only unsubscribe on offer is a web link, which has to be "
                   "opened and completed by hand.";
        case UnsubscribeAdvice::RefuseSpam:
            return "Not recommended: this is " + CategoryLabel(category) +
                   ", and using its unsubscribe link confirms someone reads this "
                   "address — which usually brings more of it, not less. Block "
                   "and delete instead.";
        case UnsubscribeAdvice::NoOffer:
            break;
    }
    return "This sender offers no unsubscribe header. Block and delete instead.";
}

// ---- Parsing ---------------------------------------------------------------

UnsubscribeInfo ParseListUnsubscribe(const std::string& headerValue) {
    UnsubscribeInfo info;

    for (const std::string& entry : SplitEntries(headerValue)) {
        if (StartsWith(entry, "mailto:")) {
            if (!info.mailto.empty()) continue;   // first mailto wins
            std::string rest = entry.substr(7);
            const size_t query = rest.find('?');
            if (query != std::string::npos) {
                const std::string params = rest.substr(query + 1);
                rest = rest.substr(0, query);
                // Pull out subject=, which some lists require to be echoed.
                const std::string needle = "subject=";
                const size_t at = Lower(params).find(needle);
                if (at != std::string::npos) {
                    std::string value = params.substr(at + needle.size());
                    const size_t amp = value.find('&');
                    if (amp != std::string::npos) value = value.substr(0, amp);
                    info.mailtoSubject = Trim(value);
                }
            }
            info.mailto = Lower(Trim(rest));
        } else if (StartsWith(entry, "https://") || StartsWith(entry, "http://")) {
            if (info.httpUrl.empty()) info.httpUrl = Trim(entry);
        }
    }
    return info;
}

bool ParseListUnsubscribePost(const std::string& headerValue) {
    // RFC 8058 defines exactly one value; compare case-insensitively and
    // ignore surrounding whitespace.
    const std::string v = Lower(Trim(headerValue));
    return v.find("list-unsubscribe=one-click") != std::string::npos;
}

// ---- Judgement -------------------------------------------------------------

UnsubscribeMethod ChooseMethod(const UnsubscribeInfo& info) {
    // One-click is unattended and specified end to end, so it wins when the
    // sender has granted it *and* given a URL to post to.
    if (info.oneClick && !info.httpUrl.empty()) return UnsubscribeMethod::OneClickPost;
    // A mailto is the other form the app can complete on its own.
    if (!info.mailto.empty()) return UnsubscribeMethod::MailTo;
    // A bare link needs a browser and a person.
    if (!info.httpUrl.empty()) return UnsubscribeMethod::HttpLink;
    return UnsubscribeMethod::None;
}

UnsubscribeAdvice AdviseUnsubscribe(MessageCategory category, const UnsubscribeInfo& info) {
    // The judgement that matters, and it comes first: for the unwanted
    // families the offer is not a courtesy, it is a liveness probe. Refuse it
    // whether or not the headers look well-formed — a working one-click link
    // in a phishing message is more dangerous, not less.
    if (IsUnwanted(category)) return UnsubscribeAdvice::RefuseSpam;

    switch (ChooseMethod(info)) {
        case UnsubscribeMethod::OneClickPost:
        case UnsubscribeMethod::MailTo:
            return UnsubscribeAdvice::Recommended;
        case UnsubscribeMethod::HttpLink:
            return UnsubscribeAdvice::NeedsBrowser;
        case UnsubscribeMethod::None:
            break;
    }
    return UnsubscribeAdvice::NoOffer;
}

} // namespace EmailCleaner
