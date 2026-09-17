// include/UltraNet/UltraNetMailAddr.h
// Address handling shared by the mail plug-ins (SMTP/IMAP/POP3): turning the
// address a caller supplies into the form the SMTP *envelope* takes.
//
// A UltraNetMailMessage carries addresses the way a user writes them —
// "Erika Fröhling <erika@example.com>" — because that is what the MIME
// From:/To:/Cc: headers show. The envelope commands MAIL FROM and RCPT TO are
// a different layer: they take an addr-spec and nothing else. Handing them the
// display name too produces "<Erika Fröhling <erika@example.com>>", which is
// not an address, and the server rejects the message (Gmail answers
// 555 5.5.2 Syntax error) — the mail never leaves, with no hint as to why.
//
// Header-only: the whole of it is two string functions, and the mail plug-ins
// are separately linked DSOs that would otherwise have to pull in a core
// object for them.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstddef>
#include <string>

namespace ultranet_mailaddr {

namespace detail {

inline std::string Trim(const std::string& s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    auto space = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    while (b < e && space(s[b])) ++b;
    while (e > b && space(s[e - 1])) --e;
    return s.substr(b, e - b);
}

} // namespace detail

// The bare addr-spec: display name and angle brackets removed.
//
//   "Erika <erika@example.com>"  -> "erika@example.com"
//   "<erika@example.com>"        -> "erika@example.com"
//   "erika@example.com"          -> "erika@example.com"
//
// The last '<' starts the angle-addr, so a display name that contains one of
// its own ("Doe, John <j@x>" or a quoted phrase) does not confuse it - the
// same rule UltraNet_MimeEncodeAddress uses to find the part it must not
// encode. An address with no angle-addr is already an addr-spec and is passed
// through, whitespace trimmed; an empty one stays empty, which EnvelopeAddr
// turns into the null reverse-path a bounce needs.
inline std::string AddrSpec(const std::string& address) {
    const std::string trimmed = detail::Trim(address);
    const std::size_t lt = trimmed.rfind('<');
    if (lt != std::string::npos) {
        const std::size_t gt = trimmed.find('>', lt);
        if (gt != std::string::npos) {
            return detail::Trim(trimmed.substr(lt + 1, gt - lt - 1));
        }
    }
    return trimmed;
}

// The addr-spec in the angle brackets MAIL FROM / RCPT TO are written with.
// "" gives "<>", the null reverse-path (RFC 5321 4.5.5) that a bounce or a
// delivery notification is sent from.
inline std::string EnvelopeAddr(const std::string& address) {
    return "<" + AddrSpec(address) + ">";
}

} // namespace ultranet_mailaddr
