// Apps/UltraDBAdmin/engine/DbAdminFormat.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminFormat.h"

#include <cstdio>

namespace UltraDBAdmin {

std::string HumanSize(int64_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    const char* units[] = {"KB", "MB", "GB", "TB"};
    double size = static_cast<double>(bytes);
    int unit = -1;
    while (size >= 1024.0 && unit < 3) { size /= 1024.0; ++unit; }
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f %s", size, units[unit]);   // locale-ok: shown to the user
    return text;
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
