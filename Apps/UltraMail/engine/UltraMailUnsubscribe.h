// Apps/UltraMail/engine/UltraMailUnsubscribe.h
// "Unsubscribe" from a mailing list the way the list itself asks to be left:
// the List-Unsubscribe header (RFC 2369) names a web page and/or an address to
// mail, and List-Unsubscribe-Post (RFC 8058) says the web address takes a
// one-click POST, which leaves the list without opening a browser.
//
// Headless: only parsing lives here. The app decides what to do with the
// result (POST it, open the page, or pre-fill a message) after asking the user.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>

namespace UltraMail {

struct UnsubscribeInfo {
    // An https address that takes the RFC 8058 one-click POST
    // ("List-Unsubscribe=One-Click"); empty when the list does not offer one.
    std::string oneClickUrl;
    // The first http(s) address: a page to open in the browser.
    std::string webUrl;
    // The mailto: target, split into its parts (percent-decoded).
    std::string mailtoAddress;
    std::string mailtoSubject;
    std::string mailtoBody;

    bool Any() const { return !webUrl.empty() || !mailtoAddress.empty(); }
};

// Parse the two header values (either may be empty).
UnsubscribeInfo ParseListUnsubscribe(const std::string& listUnsubscribe,
                                     const std::string& listUnsubscribePost);

// Read both headers from a raw RFC 5322 message (the cached .eml) and parse.
UnsubscribeInfo ReadUnsubscribe(const std::string& rawMessage);

} // namespace UltraMail
