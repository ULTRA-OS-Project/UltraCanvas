// include/UltraNet/UltraNetMailAddr.h
// Address helpers for the mail protocol plug-ins. Header-only so each loadable
// plug-in DSO can use them without a shared library dependency.
//
// The SMTP envelope (MAIL FROM / RCPT TO) carries an RFC 5321 reverse/forward
// path — a *bare* addr-spec in angle brackets, never a display name. The MIME
// `From:` / `To:` headers, by contrast, keep the display name. A caller therefore
// hands the plug-in the full mailbox ("Erika <erika@example.com>") for the
// headers; EnvelopeAddr peels the addr-spec back out for the envelope so the
// server does not get "<Erika <erika@example.com>>" (which Gmail rejects with
// 555 5.5.2 Syntax error).
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstddef>
#include <string>

namespace ultranet_mailaddr {

// Return the bare addr-spec of a mailbox, wrapped in angle brackets, ready for
// CURLOPT_MAIL_FROM / CURLOPT_MAIL_RCPT:
//   "Erika <erika@example.com>" -> "<erika@example.com>"
//   "erika@example.com"         -> "<erika@example.com>"
//   "<erika@example.com>"       -> "<erika@example.com>"
// A "Display <addr>" form yields the text between the last '<' and its following
// '>'; anything else is treated as an already-bare address. Surrounding
// whitespace is trimmed. An empty input yields "<>".
inline std::string EnvelopeAddr(const std::string& mailbox) {
    auto trim = [](const std::string& s) {
        std::size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return std::string();
        std::size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    };

    std::string spec;
    const std::size_t lt = mailbox.rfind('<');
    if (lt != std::string::npos) {
        const std::size_t gt = mailbox.find('>', lt + 1);
        if (gt != std::string::npos)
            spec = mailbox.substr(lt + 1, gt - lt - 1);
        else
            spec = mailbox.substr(lt + 1);   // unbalanced: take the rest
    } else {
        spec = mailbox;                       // already a bare addr-spec
    }
    return "<" + trim(spec) + ">";
}

} // namespace ultranet_mailaddr
