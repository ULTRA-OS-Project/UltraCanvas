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
// Version: 1.1.0 - the dynamic-array functions' prefixes (_xlfn._xlws.FILTER, _xlfn.SEQUENCE ...),
//                  A1# <-> _xlfn.ANCHORARRAY(A1), @ <-> _xlfn.SINGLE(), LET / LAMBDA names
//                  <-> _xlpm.; error literals (#REF!) are no longer read as a sheet name
// Version: 1.0.0
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework

#pragma once

#include <cctype>
#include <string>
#include <vector>

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
        "CEILING.MATH", "FLOOR.MATH", "XLOOKUP", "XMATCH", "SORTBY", "UNIQUE", "SEQUENCE",
        "RANDARRAY", "LET", "LAMBDA", "MAP", "REDUCE", "SCAN", "BYROW", "BYCOL", "MAKEARRAY"};
    for (const char* n : names) {
        if (upperName == n) return true;
    }
    return false;
}

// The dynamic-array functions Excel stores with a second prefix:
// _xlfn._xlws.FILTER, _xlfn._xlws.SORT.
inline bool NeedsWorksheetPrefix(const std::string& upperName) {
    return upperName == "FILTER" || upperName == "SORT";
}

inline std::string Upper(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return s;
}

// Index of the ')' closing the '(' at `open` (strings, quoted names and
// nested brackets skipped); npos when it is not closed.
inline size_t MatchingParen(const std::string& f, size_t open) {
    int depth = 0;
    for (size_t i = open; i < f.size(); ++i) {
        const char c = f[i];
        if (c == '"') {
            std::string ignored;
            i = CopyString(f, i, ignored) - 1;
            continue;
        }
        if (c == '\'') {
            std::string ignored;
            const size_t end = ReadQuoted(f, i, ignored);
            if (end == std::string::npos) return std::string::npos;
            i = end - 1;
            continue;
        }
        if (c == '(' || c == '{') ++depth;
        else if ((c == ')' || c == '}') && --depth == 0) return i;
    }
    return std::string::npos;
}

// The arguments between the '(' at `open` and its ')', split at the commas
// of that level and trimmed.
inline std::vector<std::string> TopLevelArguments(const std::string& f, size_t open) {
    std::vector<std::string> args;
    const size_t close = MatchingParen(f, open);
    if (close == std::string::npos) return args;
    std::string current;
    int depth = 0;
    for (size_t i = open + 1; i < close; ++i) {
        const char c = f[i];
        if (c == '"') { i = CopyString(f, i, current) - 1; continue; }
        if (c == '\'') {
            std::string name;
            const size_t end = ReadQuoted(f, i, name);
            if (end == std::string::npos) break;
            current.append(f, i, end - i);
            i = end - 1;
            continue;
        }
        if (c == '(' || c == '{') ++depth;
        if (c == ')' || c == '}') --depth;
        if (c == ',' && depth == 0) { args.push_back(current); current.clear(); continue; }
        current.push_back(c);
    }
    args.push_back(current);
    for (std::string& a : args) {
        const size_t b = a.find_first_not_of(" \t");
        const size_t e = a.find_last_not_of(" \t");
        a = b == std::string::npos ? std::string() : a.substr(b, e - b + 1);
    }
    return args;
}

// The names LET and LAMBDA define in a formula (upper case): Excel stores
// them, and every use of them, with the prefix _xlpm.
inline std::vector<std::string> LetAndLambdaNames(const std::string& f) {
    std::vector<std::string> names;
    for (size_t i = 0; i < f.size(); ++i) {
        if (f[i] == '"') { std::string ignored; i = CopyString(f, i, ignored) - 1; continue; }
        if (i > 0 && IsNameChar(f[i - 1])) continue;
        const bool let = StartsWithNoCase(f.substr(i), "let(");
        const bool lambda = StartsWithNoCase(f.substr(i), "lambda(");
        if (!let && !lambda) continue;
        const size_t open = f.find('(', i);
        const std::vector<std::string> args = TopLevelArguments(f, open);
        for (size_t k = 0; k + 1 < args.size(); ++k) {
            if (let && k % 2 == 1) continue;   // LET(name, value, name, value, calculation)
            names.push_back(Upper(args[k]));
        }
    }
    return names;
}

// The end of the operand of a '@' at `i` in native text: a bracketed group,
// or a reference or a name (with the call that follows it).
inline size_t OperandEnd(const std::string& f, size_t i) {
    if (i < f.size() && f[i] == '(') {
        const size_t close = MatchingParen(f, i);
        return close == std::string::npos ? f.size() : close + 1;
    }
    while (i < f.size()) {
        const char c = f[i];
        if (c == '\'') {
            std::string name;
            const size_t end = ReadQuoted(f, i, name);
            if (end == std::string::npos) return f.size();
            i = end;
            continue;
        }
        if (IsNameChar(c) || c == '$' || c == ':' || c == '#') { ++i; continue; }
        if (c == '(') {
            const size_t close = MatchingParen(f, i);
            return close == std::string::npos ? f.size() : close + 1;
        }
        break;
    }
    return i;
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
        if (c == '#') {   // an error literal (#REF!, #N/A) stays as it is
            out.push_back(c);
            ++i;
            while (i < excel.size() && (std::isalnum(static_cast<unsigned char>(excel[i])) ||
                                        excel[i] == '/' || excel[i] == '!' || excel[i] == '?' || excel[i] == '_')) {
                out.push_back(excel[i++]);
            }
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
            bool prefixed = false;
            while (StartsWithNoCase(word, "_xlfn.") || StartsWithNoCase(word, "_xlws.") ||
                   StartsWithNoCase(word, "_xlpm.")) {
                word = word.substr(6);
                prefixed = true;
            }
            const std::string upper = Upper(word);
            if (prefixed && end < excel.size() && excel[end] == '(' &&
                (upper == "ANCHORARRAY" || upper == "SINGLE")) {
                // _xlfn.ANCHORARRAY(A1) is A1#, _xlfn.SINGLE(x) is @x.
                const size_t close = MatchingParen(excel, end);
                if (close != std::string::npos) {
                    const std::string inner = ExcelFormulaToNative(excel.substr(end + 1, close - end - 1));
                    out += upper == "SINGLE" ? "@(" + inner + ")" : inner + "#";
                    i = close + 1;
                    continue;
                }
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
// 'Sheet'!A1, and the functions Excel stores with a prefix get it. Excel
// stores the spill reference A1# as _xlfn.ANCHORARRAY(A1), the @ operator as
// _xlfn.SINGLE(...), and the names LET and LAMBDA define with _xlpm. The
// leading '=' is kept as given.
inline std::string NativeFormulaToExcel(const std::string& native) {
    using namespace ExcelFormulaDetail;
    const std::vector<std::string> localNames = LetAndLambdaNames(native);
    auto isLocalName = [&localNames](const std::string& word) {
        const std::string upper = Upper(word);
        for (const std::string& n : localNames) {
            if (n == upper) return true;
        }
        return false;
    };
    std::string out;
    out.reserve(native.size() + 8);
    // Where in `out` the last reference written starts (its sheet included),
    // for a '#' after it.
    size_t refStart = std::string::npos;
    bool afterSheet = false;
    size_t i = 0;
    while (i < native.size()) {
        const char c = native[i];
        const bool sheetBefore = afterSheet;
        afterSheet = false;
        if (c == '"') {
            i = CopyString(native, i, out);
            refStart = std::string::npos;
            continue;
        }
        if (c == '\'') {
            std::string name;
            const size_t end = ReadQuoted(native, i, name);
            if (end != std::string::npos && end < native.size() && native[end] == '.') {
                refStart = out.size();
                afterSheet = true;
                out += QuoteSheet(name) + "!";
                i = end + 1;
                continue;
            }
            out.push_back(c);
            ++i;
            continue;
        }
        if (c == '#') {
            if (refStart != std::string::npos && i > 0 &&
                std::isdigit(static_cast<unsigned char>(native[i - 1]))) {
                out.insert(refStart, "_xlfn.ANCHORARRAY(");   // A1#
                out += ")";
                ++i;
                refStart = std::string::npos;
                continue;
            }
            out.push_back(c);   // an error literal: #N/A, #REF!
            ++i;
            while (i < native.size() && (std::isalnum(static_cast<unsigned char>(native[i])) ||
                                         native[i] == '/' || native[i] == '!' || native[i] == '?' || native[i] == '_')) {
                out.push_back(native[i++]);
            }
            continue;
        }
        if (c == '@') {
            const size_t end = OperandEnd(native, i + 1);
            out += "_xlfn.SINGLE(" + NativeFormulaToExcel(native.substr(i + 1, end - i - 1)) + ")";
            i = end;
            refStart = std::string::npos;
            continue;
        }
        const unsigned char u = static_cast<unsigned char>(c);
        const bool wordStart = (std::isalpha(u) || c == '_' || c == '$') &&
                               (i == 0 || !IsNameChar(native[i - 1]) || sheetBefore);
        if (wordStart && CellReferenceAt(native, i)) {
            // A cell reference ($A$1, B2, and a range's second half).
            size_t end = i;
            if (native[end] == '$') ++end;
            while (end < native.size() && std::isalpha(static_cast<unsigned char>(native[end]))) ++end;
            if (end < native.size() && native[end] == '$') ++end;
            while (end < native.size() && std::isdigit(static_cast<unsigned char>(native[end]))) ++end;
            const bool name = end < native.size() && (IsNameChar(native[end]) || native[end] == '(');
            if (!name) {
                if (!sheetBefore && !(i > 0 && native[i - 1] == ':')) refStart = out.size();
                out.append(native, i, end - i);
                i = end;
                continue;
            }
        }
        if (c == '$') {
            out.push_back(c);
            ++i;
            continue;
        }
        if (wordStart) {
            refStart = std::string::npos;
            // A plain identifier; "Sheet1.A1" splits at the '.' before a cell
            // reference, while a function name with a dot (STDEV.S) is kept.
            size_t end = i;
            while (end < native.size() &&
                   (std::isalnum(static_cast<unsigned char>(native[end])) || native[end] == '_'))
                ++end;
            std::string word = native.substr(i, end - i);
            if (end < native.size() && native[end] == '.' && CellReferenceAt(native, end + 1)) {
                refStart = out.size();
                afterSheet = true;
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
            const std::string upper = Upper(full);
            if (isLocalName(full)) {
                out += "_xlpm." + full;   // a LET name or LAMBDA parameter, or a call of one
                i = nameEnd;
                continue;
            }
            if (look < native.size() && native[look] == '(') {
                if (NeedsWorksheetPrefix(upper)) out += "_xlfn._xlws.";
                else if (NeedsFuturePrefix(upper)) out += "_xlfn.";
                out += full;
                i = nameEnd;
                continue;
            }
            out += word;
            i = end;
            continue;
        }
        if (c != ':') refStart = std::string::npos;
        out.push_back(c);
        ++i;
    }
    return out;
}

} // namespace UltraCanvas
