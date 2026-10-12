// Apps/UltraMail/engine/UltraMailOneTimeCode.h
// The one-time codes in a message: the code a sign-in, a payment or an
// address check sends by mail to be typed in somewhere else ("Dein
// Anmelde-Code: 649082", "Your verification code is 482913", "您的验证码是
// 649082"). The reading pane puts a copy button on each one it finds.
//
// Language independent: the code itself is recognised by its shape (4 to 10
// digits, or a short run of capitals and digits, alone or in groups), and the
// word that says it is a code is looked up in a list covering the major
// languages and scripts. A code is reported when
//   - it is the whole of a block of the message (a paragraph of its own, the
//     line of a plain-text mail) and the message speaks of a code anywhere -
//     subject or body; or
//   - it stands close to that word in the same block, before or after it,
//     with no other number in between.
// Numbers that only look like codes are left alone: years, decimals, times,
// IP addresses, prices, parts of addresses and links, "#order" numbers, and
// what follows "postal code", "zip code", "error code" and the like.
// Version: 0.1.0
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace UltraMail {

struct OneTimeCode {
    std::string code;          // as it is typed in: "123 456" without its space
    std::string shown;         // as the message writes it
    int  block = -1;           // the block it is in (index into `blocks`); -1: the subject
    bool standalone = false;   // that block holds the code and nothing else
};

// The one-time codes of a message, best first, at most `maxCodes`, each code
// once. `blocks` is the body's text in reading order, a block at a time: an
// HTML mail's paragraphs (one per label of the built page), a plain-text
// mail's lines.
std::vector<OneTimeCode> FindOneTimeCodes(const std::string& subject,
                                          const std::vector<std::string>& blocks,
                                          std::size_t maxCodes = 3);

// Whether a text speaks of a code, in any language the list knows: "code",
// "Anmelde-Code", "código", "код", "κωδικός", "验证码", "認証コード",
// "인증번호", "รหัส", "رمز", "קוד", "OTP", "PIN", "TAN", "verification" ...
bool MentionsOneTimeCode(const std::string& text);

} // namespace UltraMail
