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
// Version: 0.6.0 - romance scams (romance-scam: a stranger's love letter, with
//                  photos, from a free mailbox) and cryptocurrency (crypto-content
//                  on any crypto mail; crypto-wallet-secret, crypto-payment-demand,
//                  crypto-investment-lure); ScanInput::pictureNames;
//                  ThreatReport::Has / Codes, so the reading pane can name the scam
// Version: 0.5.0 - mail authentication: the receiving server's (topmost)
//                  Authentication-Results header is parsed
//                  (ParseAuthenticationResults); a sender whose domain DMARC
//                  or an aligned DKIM signature proves (VerifiedSenderDomain)
//                  is not flagged for its mail service's tracking links, its
//                  reply address or its many link domains, and a proven
//                  registry brand not for asking to update account details;
//                  auth-failure only when DMARC fails, or nothing passes
// Version: 0.4.0 - plain text: mail addresses (mailto:) are links too
// Version: 0.3.0 - PlainLinkAt (the bare URL under the pointer in plain text)
// Version: 0.2.0 - borrowed-pictures rule; kThreatRulesRevision (re-scan older verdicts);
//                ExtractImageHosts
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
    // The From domain the receiving server proved genuine ("" when nothing
    // proved it), and how ("DKIM signature", "DMARC", "DKIM signature and
    // DMARC"). Not a finding: it says the address is real, not that the
    // message is harmless.
    std::string                verifiedDomain;
    std::string                verifiedBy;

    // The findings as one human-readable block ("• …\n• …"); empty when clean.
    std::string Summary() const;
    // The findings' codes, comma-separated ("romance-scam,crypto-content"):
    // what the store keeps so the reading pane can say which scam it is.
    std::string Codes() const;
    bool Has(const std::string& code) const;
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
// their anchor text; plain-text bodies give the bare URLs and mail addresses
// (as mailto:, with no host).
std::vector<MessageLink> ExtractLinks(const std::string& body, bool isHtml);

// The link (as ExtractLinks finds it in plain text: a web address, a mailto:
// or a bare mail address, which comes back as "mailto:name@example.com") that
// covers byte `offset` of `text`, or "" when that byte is not part of one -
// what the plain-text view reports for the pointer.
std::string PlainLinkAt(const std::string& text, std::size_t offset);

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
    std::string authResults;     // the topmost Authentication-Results header's value:
                                 // the receiving server's own (one further down
                                 // may come from anyone, the sender included)
    std::string body;
    bool        bodyIsHtml = false;
    std::vector<std::string> attachmentNames;
    // The pictures `attachmentNames` does not show as such, by file name (""
    // when one has none): the body's own pictures (cid:), and an image
    // attached under a name that is not a picture's. An attachment named
    // like one ("IMG_942.jpg") counts from `attachmentNames` already.
    std::vector<std::string> pictureNames;
};

// When the rules last changed (epoch seconds). A stored verdict made before it
// came from older rules: the reader scans the message again when it is opened,
// and the sync re-scans the stored bodies a batch at a time
// (SyncEngine::RescanStaleVerdicts), so a phishing mail an earlier version let
// through is caught, and a genuine one it flagged is cleared.
constexpr long long kThreatRulesRevision = 1791504000;   // 2026-10-09 00:00 UTC

// ---------------------------------------------------------------------------
// Mail authentication
// ---------------------------------------------------------------------------
// What one Authentication-Results header (RFC 8601) reports - the checks the
// receiving server made of the sending domain's own records. Comments are
// dropped; methods, results and domains are lowercased.
struct AuthResults {
    std::string authservId;   // who checked: "mx.google.com"
    std::string dmarc;        // "pass", "fail", "none", ... ("" when not reported)
    std::string dmarcFrom;    // the From domain DMARC was evaluated for (header.from)
    std::string spf;          // the SPF result
    std::string spfDomain;    // the envelope sender's domain (smtp.mailfrom)
    // Every DKIM signature: its result and the domain that signed (header.d,
    // else the domain of header.i).
    std::vector<std::pair<std::string, std::string>> dkim;
};
AuthResults ParseAuthenticationResults(const std::string& headerValue);

// The From domain the receiving server proved genuine: DMARC passed for it,
// or a DKIM signature by its registrable domain verified (a mail service's
// own signature proves nothing about the From address). "" when nothing
// proves it, or DMARC failed. `method` gets "DKIM signature", "DMARC" or
// "DKIM signature and DMARC".
std::string VerifiedSenderDomain(const AuthResults& auth, const std::string& fromDomain,
                                 std::string* method = nullptr);

// The first (topmost) value of header `name` in a raw message's header block,
// folded lines joined; "" when it has none.
std::string TopHeaderValue(const std::string& rawMessage, const std::string& name);

// One check, as the reading pane shows it: a small pill per method
// ("DMARC", "DKIM", "SPF") - passed, failed or no clear answer - whose
// tooltip says what was checked, for which domain, what that proves and who
// checked it.
enum class AuthCheckState { Passed, Failed, Neutral };
struct AuthCheck {
    std::string    label;     // "DMARC", "DKIM", "SPF", "S/MIME", "Not checked"
    AuthCheckState state = AuthCheckState::Neutral;
    std::string    tooltip;   // a few lines, ready to show
};

// The pills for one message: DMARC, DKIM and SPF as the receiving server
// reported them (in that order, the strongest first), or a single neutral
// "Not checked" when it reported none. `fromDomain` is the From address's.
std::vector<AuthCheck> DescribeAuthentication(const AuthResults& auth,
                                              const std::string& fromDomain);

// How a raw message is signed by its author: "S/MIME" (multipart/signed with
// a PKCS #7 signature, or signed application/pkcs7-mime), "OpenPGP", or "".
// Detected, not verified: the signature and its certificate are not checked.
std::string MessageSignatureKind(const std::string& rawMessage);

// Everything above for a raw message: the authentication pills, then the
// author's signature when there is one.
std::vector<AuthCheck> DescribeMessageAuthentication(const std::string& rawMessage);

// The hosts the body's pictures (<img src>, background images) are loaded from,
// lowercased; http(s) sources only.
std::vector<std::string> ExtractImageHosts(const std::string& body);

// Run every rule over one message.
ThreatReport ScanMessage(const ScanInput& input);

// Whether `codes` (ThreatReport::Codes, as the store keeps it) holds `code`.
bool HasFindingCode(const std::string& codes, const std::string& code);

// Convenience: parse a raw RFC 5322 message (the cached .eml) and scan it.
ThreatReport ScanRawMessage(const std::string& rawMessage);

// The sender's domain next to the domain the message's button actually goes
// to, for the reading pane's warning ("Sender domain: … / Button domain: …").
// `found` is false when the sender has no domain or every link stays on it.
// The button is the first off-domain link that carries text (an "unsubscribe"
// footer link is passed over while a better one exists); a bare off-domain
// URL is the fallback, reported with `isButton` false.
struct DomainMismatch {
    bool        found = false;
    std::string senderDomain;    // "example.com"
    std::string linkDomain;      // the link's host, "login.example-verify.top"
    std::string linkText;        // the button's text, when it has one
    bool        isButton = false;
};

DomainMismatch FindDomainMismatch(const ScanInput& input);
DomainMismatch FindDomainMismatchInRaw(const std::string& rawMessage);

} // namespace UltraMail
