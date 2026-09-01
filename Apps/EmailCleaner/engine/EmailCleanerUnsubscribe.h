// Apps/EmailCleaner/engine/EmailCleanerUnsubscribe.h
// Reading a sender's unsubscribe offer, and deciding whether taking it up is
// actually a good idea.
//
// The parsing is RFC 2369 (List-Unsubscribe) plus RFC 8058 (the one-click
// List-Unsubscribe-Post). The judgement is the part that matters:
//
//   Unsubscribing from a newsletter you once opted into works, and is the
//   polite way out. Unsubscribing from *spam* does the opposite of what the
//   user wants — it confirms a human reads that address, which is exactly the
//   signal a spam operation is looking for, and typically increases the
//   traffic. So for the unwanted families this module recommends block and
//   delete, and reports the unsubscribe offer as bait rather than as an
//   action.
//
// Pure: no network, no database, no UI. Sending the request is the executor's
// job (EmailCleanerActions); deciding whether to is this module's.
// Version: 0.2.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "EmailCleanerTypes.h"

#include <string>

namespace EmailCleaner {

// What a message's List-Unsubscribe headers offer.
struct UnsubscribeInfo {
    std::string mailto;     // address from a <mailto:...> entry (without the scheme)
    std::string mailtoSubject;  // ?subject= from the mailto, when present
    std::string httpUrl;    // https/http entry, verbatim
    bool        oneClick = false;  // List-Unsubscribe-Post: List-Unsubscribe=One-Click

    bool Any() const { return !mailto.empty() || !httpUrl.empty(); }
};

// How an unsubscribe would be carried out.
enum class UnsubscribeMethod {
    None = 0,      // nothing on offer
    OneClickPost,  // RFC 8058: POST "List-Unsubscribe=One-Click" to httpUrl
    MailTo,        // send a message to `mailto` (the app can do this unattended)
    HttpLink       // an https URL that needs a browser and a human
};

std::string ToString(UnsubscribeMethod method);

// Whether unsubscribing is the right move.
enum class UnsubscribeAdvice {
    Recommended = 0,  // opt-in bulk mail with a real unsubscribe offer
    NoOffer,          // nothing to act on
    NeedsBrowser,     // only a plain link — a human has to finish it
    RefuseSpam        // an unwanted family: taking the offer confirms the address
};

std::string ToString(UnsubscribeAdvice advice);

// One sentence explaining the advice, for the confirmation dialog.
std::string DescribeAdvice(UnsubscribeAdvice advice, MessageCategory category);

// ---- Parsing ---------------------------------------------------------------

// Parse an RFC 2369 List-Unsubscribe value: a comma-separated list of URIs in
// angle brackets, e.g. "<mailto:bye@list.example?subject=unsub>, <https://...>".
// Tolerates missing brackets, extra whitespace and unknown schemes.
UnsubscribeInfo ParseListUnsubscribe(const std::string& headerValue);

// True when a List-Unsubscribe-Post value grants RFC 8058 one-click.
bool ParseListUnsubscribePost(const std::string& headerValue);

// ---- Judgement -------------------------------------------------------------

// The method that would be used, given what is on offer. One-click first (it
// is unattended and standardised), then mailto (also unattended), then the
// bare link (needs a person).
UnsubscribeMethod ChooseMethod(const UnsubscribeInfo& info);

// Whether to offer unsubscribing at all, for a sender of this category.
UnsubscribeAdvice AdviseUnsubscribe(MessageCategory category, const UnsubscribeInfo& info);

} // namespace EmailCleaner
