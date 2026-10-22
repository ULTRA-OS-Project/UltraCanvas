// Apps/UltraDBAdmin/engine/DbAdminFormat.h
// How a database value reads in a grid cell. A cell is for looking at, so it
// is short and unambiguous: NULL is not the empty string, a blob says how big
// it is rather than dumping bytes, and a long text is cut at a line.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <UltraDatabase/UltraDatabase.h>

#include <cstddef>
#include <string>

namespace UltraDBAdmin {

// The word a NULL is shown as, distinct from any text a column can hold.
inline const char* NullText() { return "NULL"; }

// One value as one line: NULL -> "NULL"; a blob -> "<blob, 1.2 KB>"; true /
// false; numbers as the database wrote them (dot-decimal, see
// UltraDbValue::AsString); text up to the first line break and at most
// `maxChars` characters (UTF-8 kept whole), "…" marking a cut.
std::string CellText(const UltraDbValue& value, size_t maxChars = 200);

// "0 B", "512 B", "1.2 KB", "3.4 MB" - for sizes the user reads.
std::string HumanSize(int64_t bytes);

} // namespace UltraDBAdmin
