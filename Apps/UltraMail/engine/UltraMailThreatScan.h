// Apps/UltraMail/engine/UltraMailThreatScan.h
// The content scan behind UltraMail's sender badge: it reads a message the way
// a suspicious reader would — where do the links and buttons actually go, does
// the text of a link agree with its target, does the mail claim to be from a
// brand its From address does not belong to — and returns a level plus the
// reasons for it, in words the badge's tooltip and the reading pane's warning
// can show verbatim.
//
// Headless and dependency-light on purpose: everything here runs on a raw
// RFC 5322 message or on the strings the caller already has, so the suite can
// exercise every rule without a mail server, a window or a database.
//
// It is a heuristic, and it is deliberately asymmetric: a false "suspicious"
// costs the user a second look, a missed phishing mail can cost them their
// account. But it only ever *labels* a message — nothing here deletes, moves
// or blocks mail, and the reasons are always shown so the user can disagree.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>
#include <vector>

namespace UltraMail {

// How the scan reads a message. Ordered: a higher value is a worse verdict,
// which is what the badge and the store's rollups compare on.
enum class ThreatLevel {
    Unscanned     = -1,   // no body has been scanned yet (nothing is claimed)
    Clean         = 0,    // nothing stood out
    Advertisement = 1,    // bulk / marketing mail (List-Unsubscribe, bulk …)
    Suspicious    = 2,    // spam-ish, or link behaviour that does not add up
    Scam          = 3     // phishing / scam markers: links that lie about where they go
};

std::string ToString(ThreatLevel level);
ThreatLevel ThreatLevelFromString(const std::string& s);

// One reason, as shown to the user. `code` is the stable identifier (for tests
// and for the docs table); `detail` is the sentence the tooltip shows.
struct ThreatFinding {
    std::string code;
    std::string detail;
};

struct ThreatReport {
    ThreatLevel                level = ThreatLevel::Unscanned;
    int                        score = 0;
    bool                       bulk  = false;   // List-Unsubscribe / Precedence: bulk
    std::vector<ThreatFinding> findings;

    // The findings as one human-readable block ("• …\n• …"); empty when clean.
    std::string Summary() const;
    bool Suspicious() const { return level >= ThreatLevel::Suspicious; }
};

// One link found in the body — an <a href>, a <form action>, a button wrapped
// in a link, or a bare URL in plain text.
struct MessageLink {
    std::string href;   // the target, as written
    std::string text;   // the anchor text (empty for a bare URL / a form)
    std::string host;   // lowercased host of `href` ("" when it has none)
};

// Pull every link out of a body. HTML bodies give href/action targets with
// their anchor text; plain-text bodies give the bare URLs.
std::vector<MessageLink> ExtractLinks(const std::string& body, bool isHtml);

// What the scan needs about a message. Everything is optional: a caller that
// has only a body still gets the link rules.
struct ScanInput {
    std::string fromAddr;        // envelope From address
    std::string fromName;        // display name
    std::string subject;
    std::string replyTo;         // Reply-To header value
    std::string listUnsubscribe; // List-Unsubscribe header value
    std::string precedence;      // Precedence header value
    std::string autoSubmitted;   // Auto-Submitted header value
    std::string spamFlag;        // X-Spam-Flag value
    std::string spamStatus;      // X-Spam-Status / SpamAssassin summary
    std::string authResults;     // Authentication-Results value
    std::string body;
    bool        bodyIsHtml = false;
    std::vector<std::string> attachmentNames;
};

// Run every rule over one message.
ThreatReport ScanMessage(const ScanInput& input);

// Convenience: parse a raw RFC 5322 message (the cached .eml) and scan it.
ThreatReport ScanRawMessage(const std::string& rawMessage);

} // namespace UltraMail
