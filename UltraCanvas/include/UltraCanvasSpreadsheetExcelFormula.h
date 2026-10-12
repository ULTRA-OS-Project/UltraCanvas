// include/UltraCanvasSpreadsheetExcelFormula.h
// Translation between the formula text Excel stores (the <f> of an .xlsx
// worksheet) and the spreadsheet engine's native syntax - the counterpart of
// UltraCanvasSpreadsheetOdsFormula.h for OOXML.
//
// The two differ in how a reference names another sheet:
//     Excel:   Data!B5     'Sales 2024'!A1:B9     'It''s'!A1
//     native:  'Data'.B5   'Sales 2024'.A1:B9     'It''s'.A1
// and Excel writes the functions it added in 2010 and later with a prefix in
// the file (_xlfn.IFS, _xlfn.CONCAT, _xlws.FILTER), which a formula bar never
// shows. Everything else - A1 references, operators, string literals, function
// calls - is written the same way, so only these spots are rewritten; string
// literals are left as they are.
//
// Version: 1.0.0
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework

#pragma once

#include <cctype>
#include <string>

namespace UltraCanvas {

namespace ExcelFormulaDetail {

inline bool IsNameChar(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return std::isalnum(u) || c == '_' || c == '.' || u >= 0x80;
}

inline std::string QuoteSheet(const std::string& name) {
    std::string out = "'";
    for (char c : name) {
        out.push_back(c);
        if (c == '\'') out.push_back('\'');
    }
    out.push_back('\'');
    return out;
}

// Copies a "..." literal (with "" escapes) starting at `i`; returns the index
// after it.
inline size_t CopyString(const std::string& f, size_t i, std::string& out) {
    const size_t start = i++;
    while (i < f.size()) {
        if (f[i] == '"') {
            if (i + 1 < f.size() && f[i + 1] == '"') { i += 2; continue; }
            ++i;
            break;
        }
        ++i;
    }
    out.append(f, start, i - start);
    return i;
}

// Reads a '...' name (with '' escapes) starting at `i`; returns the index
// after its closing quote, or std::string::npos when it is not closed.
inline size_t ReadQuoted(const std::string& f, size_t i, std::string& name) {
    name.clear();
    ++i;
    while (i < f.size()) {
        if (f[i] == '\'') {
            if (i + 1 < f.size() && f[i + 1] == '\'') { name.push_back('\''); i += 2; continue; }
            return i + 1;
        }
        name.push_back(f[i++]);
    }
    return std::string::npos;
}

// True when a cell reference ([$]col letters[$]row digits) starts at `i`.
inline bool CellReferenceAt(const std::string& f, size_t i) {
    if (i < f.size() && f[i] == '$') ++i;
    size_t letters = 0;
    while (i < f.size() && std::isalpha(static_cast<unsigned char>(f[i]))) { ++i; ++letters; }
    if (letters == 0 || letters > 3) return false;
    if (i < f.size() && f[i] == '$') ++i;
    return i < f.size() && std::isdigit(static_cast<unsigned char>(f[i]));
}

inline bool StartsWithNoCase(const std::string& s, const char* prefix) {
    size_t i = 0;
    for (; prefix[i]; ++i) {
        if (i >= s.size() ||
            std::tolower(static_cast<unsigned char>(s[i])) != static_cast<unsigned char>(prefix[i]))
            return false;
    }
    return true;
}

// Functions newer than Excel 2007 that the engine has: Excel stores them with
// _xlfn. and reads a bare name as #NAME?.
inline bool NeedsFuturePrefix(const std::string& upperName) {
    static const char* const names[] = {
        "IFS", "SWITCH", "CONCAT", "TEXTJOIN", "MAXIFS", "MINIFS", "XOR", "IFNA", "DAYS",
        "ISOWEEKNUM", "STDEV.S", "STDEV.P", "VAR.S", "VAR.P", "MODE.SNGL", "RANK.EQ",
        "CEILING.MATH", "FLOOR.MATH", "XLOOKUP"};
    for (const char* n : names) {
        if (upperName == n) return true;
    }
    return false;
}

} // namespace ExcelFormulaDetail

// Excel formula text (with or without its leading '=') into the engine's
// syntax: sheet-qualified references become 'Sheet'.A1 and the _xlfn. /
// _xlws. prefixes are dropped. The leading '=' is kept as given.
inline std::string ExcelFormulaToNative(const std::string& excel) {
    using namespace ExcelFormulaDetail;
    std::string out;
    out.reserve(excel.size() + 8);
    size_t i = 0;
    while (i < excel.size()) {
        const char c = excel[i];
        if (c == '"') {
            i = CopyString(excel, i, out);
            continue;
        }
        if (c == '\'') {
            std::string name;
            const size_t end = ReadQuoted(excel, i, name);
            if (end != std::string::npos && end < excel.size() && excel[end] == '!') {
                out += QuoteSheet(name) + ".";
                i = end + 1;
                continue;
            }
            out.push_back(c);
            ++i;
            continue;
        }
        const bool wordStart = IsNameChar(c) && c != '.' && (i == 0 || !IsNameChar(excel[i - 1]));
        if (wordStart) {
            size_t end = i;
            while (end < excel.size() && IsNameChar(excel[end])) ++end;
            std::string word = excel.substr(i, end - i);
            if (end < excel.size() && excel[end] == '!' &&
                !std::isdigit(static_cast<unsigned char>(word[0]))) {
                out += QuoteSheet(word) + ".";
                i = end + 1;
                continue;
            }
            if (StartsWithNoCase(word, "_xlfn.") || StartsWithNoCase(word, "_xlws.")) {
                word = word.substr(6);
            }
            out += word;
            i = end;
            continue;
        }
        out.push_back(c);
        ++i;
    }
    return out;
}

// The engine's formula text into Excel's: 'Sheet'.A1 and Sheet.A1 become
// 'Sheet'!A1, and the functions Excel stores with a prefix get it. The
// leading '=' is kept as given.
inline std::string NativeFormulaToExcel(const std::string& native) {
    using namespace ExcelFormulaDetail;
    std::string out;
    out.reserve(native.size() + 8);
    size_t i = 0;
    while (i < native.size()) {
        const char c = native[i];
        if (c == '"') {
            i = CopyString(native, i, out);
            continue;
        }
        if (c == '\'') {
            std::string name;
            const size_t end = ReadQuoted(native, i, name);
            if (end != std::string::npos && end < native.size() && native[end] == '.') {
                out += QuoteSheet(name) + "!";
                i = end + 1;
                continue;
            }
            out.push_back(c);
            ++i;
            continue;
        }
        const unsigned char u = static_cast<unsigned char>(c);
        const bool wordStart = (std::isalpha(u) || c == '_') &&
                               (i == 0 || !IsNameChar(native[i - 1]));
        if (wordStart) {
            // A plain identifier; "Sheet1.A1" splits at the '.' before a cell
            // reference, while a function name with a dot (STDEV.S) is kept.
            size_t end = i;
            while (end < native.size() &&
                   (std::isalnum(static_cast<unsigned char>(native[end])) || native[end] == '_'))
                ++end;
            std::string word = native.substr(i, end - i);
            if (end < native.size() && native[end] == '.' && CellReferenceAt(native, end + 1)) {
                out += word + "!";
                i = end + 1;
                continue;
            }
            // The whole dotted name, to tell whether it is a function call.
            size_t nameEnd = end;
            while (nameEnd < native.size() && IsNameChar(native[nameEnd])) ++nameEnd;
            std::string full = native.substr(i, nameEnd - i);
            size_t look = nameEnd;
            while (look < native.size() && native[look] == ' ') ++look;
            if (look < native.size() && native[look] == '(') {
                std::string upper = full;
                for (char& ch : upper) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                if (NeedsFuturePrefix(upper)) out += "_xlfn.";
                out += full;
                i = nameEnd;
                continue;
            }
            out += word;
            i = end;
            continue;
        }
        out.push_back(c);
        ++i;
    }
    return out;
}

} // namespace UltraCanvas
