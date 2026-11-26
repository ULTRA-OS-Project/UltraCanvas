// Apps/UltraDBAdmin/engine/DbAdminFormat.cpp
// Version: 0.1.1 - sizes through UltraLocalisation::FormatFileSize, in the person's language and conventions
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminFormat.h"
#include "UltraLocalisation/UltraLocalisation.h"   // FormatFileSize

#include <cstdio>

namespace UltraDBAdmin {

std::string HumanSize(int64_t bytes) {
    return UltraCanvas::UltraLocalisation::FormatFileSize(bytes > 0 ? static_cast<unsigned long long>(bytes) : 0ULL);   // "1,2 MB" in German
}

std::string CellText(const UltraDbValue& value, size_t maxChars) {
    switch (value.Type()) {
        case UltraDbType::Null: return NullText();
        case UltraDbType::Boolean: return value.AsBool() ? "true" : "false";
        case UltraDbType::Blob:
            return "<blob, " + HumanSize(static_cast<int64_t>(value.AsBlob().size())) + ">";
        case UltraDbType::Int:
        case UltraDbType::Double:
            return value.AsString();
        case UltraDbType::Text: break;
    }
    const std::string text = value.AsString();
    std::string out;
    size_t chars = 0;
    for (size_t i = 0; i < text.size(); ) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '\n' || c == '\r') return out + "…";
        // Whole UTF-8 sequences only: a cut mid-character would show as junk.
        const size_t length = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        if (chars == maxChars) return out + "…";
        out.append(text, i, length);
        i += length;
        ++chars;
    }
    return out;
}

} // namespace UltraDBAdmin
