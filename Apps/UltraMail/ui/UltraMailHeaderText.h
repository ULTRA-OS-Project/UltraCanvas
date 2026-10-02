// Apps/UltraMail/ui/UltraMailHeaderText.h
// A header value (subject, sender name, recipient) as the UI shows it.
// Version: 0.1.0
// Last Modified: 2026-10-02
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "HTMLReader/HTMLDocument.h"

#include <UltraNet/UltraNetMime.h>

#include <string>

namespace UltraMail {

// RFC 2047 encoded-words decoded, then HTML character references: some
// senders' mailing systems write the name into the header as HTML -
// Lexware's "Stefan Fr&ouml;hling" - which no mail client should show raw.
// An unknown "&name;" and a bare "&" (AT&T) are kept as they are. Idempotent
// on text already decoded, so messages stored either way read the same.
inline std::string DisplayHeader(const std::string& raw) {
    std::string text = UltraNet_MimeDecodeHeader(raw);
    if (text.find('&') == std::string::npos) return text;
    return UltraCanvas::HTML::DecodeEntities(text);
}

} // namespace UltraMail
