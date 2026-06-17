// include/UltraCanvasCSVImport.h
// Self-contained CSV/TSV import: options, auto-detection and a robust parser.
// Shared by the spreadsheet loader, the file-manager preview and the manual
// "Text Import" options dialog. Header-only and dependency-free (std only) so
// it can be reused by any module without pulling in the spreadsheet stack.
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include <string>
#include <vector>
#include <set>
#include <map>
#include <array>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <initializer_list>
#include "UltraCanvasFileError.h"

namespace UltraCanvas {

// ============================================================================
// CSV IMPORT OPTIONS
// ============================================================================

// Mirrors the fields of a typical "Text Import" dialog (charset, start row,
// separators, text delimiter, number recognition). A set of separator
// characters is supported simultaneously, matching the Tab/Comma/Semicolon/
// Space/Other checkboxes offered by desktop spreadsheets.
struct CSVImportOptions {
    enum class Encoding {
        UTF8,        // Unicode (UTF-8), also the default when a BOM is absent
        UTF16LE,
        UTF16BE,
        Latin1,      // ISO-8859-1 / Western Europe
        Windows1252  // superset of Latin-1 used by Excel on Windows
    };

    Encoding encoding = Encoding::UTF8;

    // 1-based first row to import ("Ab Zeile"). Rows above are skipped.
    int startRow = 1;

    // Separated vs. fixed-width. Fixed width uses columnBreaks (character
    // offsets); separated mode uses the separator set below.
    bool fixedWidth = false;
    std::vector<int> columnBreaks;

    // Active field separators ("Trennoptionen"). Any combination is allowed.
    std::set<char> separators = { ',' };

    // Collapse runs of separators into one ("Feldtrenner zusammenfassen").
    bool mergeDelimiters = false;

    // Text delimiter / quote character ("Texttrenner"). 0 disables quoting.
    char textDelimiter = '"';

    // Treat quoted values strictly as text, never as numbers
    // ("Werte in Hochkomma als Text").
    bool quotedAsText = false;

    // Extended number recognition ("Erweiterte Zahlenerkennung"): when on, the
    // decimal/thousands separators below are used to recognise localised numbers
    // such as "1.234,56".
    bool detectNumbers = true;
    char decimalSeparator = '.';
    char thousandsSeparator = ',';

    // Convenience helpers --------------------------------------------------
    void SetSingleSeparator(char c) { separators.clear(); separators.insert(c); }
    bool HasSeparator(char c) const { return separators.count(c) > 0; }
};

// ============================================================================
// ENCODING HELPERS
// ============================================================================

namespace CSVDetail {

// Append the UTF-8 encoding of a Unicode code point to out.
inline void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// CP1252 differs from Latin-1 only in the 0x80-0x9F range; map those few code
// points explicitly and treat everything else as ISO-8859-1.
inline uint32_t Cp1252ToUnicode(unsigned char c) {
    switch (c) {
        case 0x80: return 0x20AC; case 0x82: return 0x201A; case 0x83: return 0x0192;
        case 0x84: return 0x201E; case 0x85: return 0x2026; case 0x86: return 0x2020;
        case 0x87: return 0x2021; case 0x88: return 0x02C6; case 0x89: return 0x2030;
        case 0x8A: return 0x0160; case 0x8B: return 0x2039; case 0x8C: return 0x0152;
        case 0x8E: return 0x017D; case 0x91: return 0x2018; case 0x92: return 0x2019;
        case 0x93: return 0x201C; case 0x94: return 0x201D; case 0x95: return 0x2022;
        case 0x96: return 0x2013; case 0x97: return 0x2014; case 0x98: return 0x02DC;
        case 0x99: return 0x2122; case 0x9A: return 0x0161; case 0x9B: return 0x203A;
        case 0x9C: return 0x0153; case 0x9E: return 0x017E; case 0x9F: return 0x0178;
        default:   return c;  // identical to Latin-1 elsewhere
    }
}

} // namespace CSVDetail

// Convert raw file bytes to UTF-8 according to the chosen encoding, stripping
// any byte-order mark. UTF-8 input is passed through unchanged (minus BOM).
inline std::string CSVDecodeToUtf8(const std::string& raw, CSVImportOptions::Encoding enc) {
    using E = CSVImportOptions::Encoding;
    std::string out;

    auto startsWith = [&](std::initializer_list<unsigned char> bytes) {
        if (raw.size() < bytes.size()) return false;
        size_t i = 0;
        for (unsigned char b : bytes) {
            if (static_cast<unsigned char>(raw[i++]) != b) return false;
        }
        return true;
    };

    switch (enc) {
        case E::UTF16LE:
        case E::UTF16BE: {
            size_t i = 0;
            bool be = (enc == E::UTF16BE);
            if (startsWith({0xFF, 0xFE})) { be = false; i = 2; }
            else if (startsWith({0xFE, 0xFF})) { be = true; i = 2; }
            for (; i + 1 < raw.size(); i += 2) {
                unsigned char b0 = raw[i], b1 = raw[i + 1];
                uint32_t cp = be ? (b0 << 8 | b1) : (b1 << 8 | b0);
                CSVDetail::AppendUtf8(out, cp);
            }
            break;
        }
        case E::Latin1: {
            for (unsigned char c : raw) CSVDetail::AppendUtf8(out, c);
            break;
        }
        case E::Windows1252: {
            for (unsigned char c : raw) CSVDetail::AppendUtf8(out, CSVDetail::Cp1252ToUnicode(c));
            break;
        }
        case E::UTF8:
        default: {
            size_t i = 0;
            if (startsWith({0xEF, 0xBB, 0xBF})) i = 3;  // strip UTF-8 BOM
            out.assign(raw.begin() + i, raw.end());
            break;
        }
    }
    return out;
}

// Backward-compatible alias: the generic reader-error describer lives in
// UltraCanvasFileError.h now and is shared by every loader.
inline std::string CSVDescribeOpenError(const std::string& path) {
    return DescribeFileReadError(path);
}

// Read a whole file into memory (binary) so encoding detection sees the BOM.
// On failure 'error' (when provided) receives a descriptive reason.
inline bool CSVReadFileRaw(const std::string& path, std::string& out,
                           std::string* error = nullptr) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        if (error) *error = CSVDescribeOpenError(path);
        return false;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    out = ss.str();
    return true;
}

// ============================================================================
// PARSER
// ============================================================================

// A single parsed field plus whether it was wrapped in the text delimiter.
// The quoted flag lets callers honour "treat quoted values as text".
struct CSVField {
    std::string text;
    bool quoted = false;
};
using CSVRow = std::vector<CSVField>;
using CSVGrid = std::vector<CSVRow>;

// Parse already-UTF-8 text into a grid of fields. Handles quoted fields
// (including embedded separators and newlines), doubled quotes as escapes,
// multiple simultaneous separators, separator merging and the start-row offset.
inline CSVGrid CSVParse(const std::string& text, const CSVImportOptions& opt) {
    CSVGrid rows;
    CSVRow current;
    std::string field;
    bool inQuotes = false;
    bool fieldWasQuoted = false;
    bool sawAnyFieldChar = false;

    const char quote = opt.textDelimiter;
    const bool quoting = quote != 0;

    auto isSep = [&](char c) { return opt.separators.count(c) > 0; };

    auto pushField = [&]() {
        current.push_back(CSVField{ field, fieldWasQuoted });
        field.clear();
        fieldWasQuoted = false;
    };
    auto pushRow = [&]() {
        pushField();
        rows.push_back(std::move(current));
        current.clear();
        sawAnyFieldChar = false;
    };

    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];

        if (inQuotes) {
            if (quoting && c == quote) {
                if (i + 1 < text.size() && text[i + 1] == quote) {
                    field += quote;  // escaped quote
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                field += c;
            }
            continue;
        }

        if (quoting && c == quote && field.empty()) {
            inQuotes = true;
            fieldWasQuoted = true;
            sawAnyFieldChar = true;
            continue;
        }

        if (c == '\r') {
            // Normalise CRLF / CR line endings.
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
            pushRow();
            continue;
        }
        if (c == '\n') {
            pushRow();
            continue;
        }

        if (!opt.fixedWidth && isSep(c)) {
            if (opt.mergeDelimiters && field.empty() && !fieldWasQuoted) {
                // Collapse leading/consecutive separators: skip empty field.
                continue;
            }
            pushField();
            continue;
        }

        field += c;
        sawAnyFieldChar = true;
    }

    // Flush the trailing row unless the file ended exactly on a newline.
    if (!field.empty() || !current.empty() || sawAnyFieldChar) {
        pushRow();
    }

    // Apply fixed-width splitting if requested (operates per raw line).
    if (opt.fixedWidth) {
        CSVGrid fw;
        for (auto& r : rows) {
            // In fixed-width mode each "row" holds the full line in field 0.
            std::string line = r.empty() ? std::string() : r[0].text;
            CSVRow cols;
            int prev = 0;
            for (int brk : opt.columnBreaks) {
                if (brk > prev && brk <= static_cast<int>(line.size())) {
                    cols.push_back(CSVField{ line.substr(prev, brk - prev), false });
                    prev = brk;
                } else if (brk > prev) {
                    cols.push_back(CSVField{ line.substr(prev), false });
                    prev = static_cast<int>(line.size());
                }
            }
            cols.push_back(CSVField{ line.substr(std::min<size_t>(prev, line.size())), false });
            fw.push_back(std::move(cols));
        }
        rows.swap(fw);
    }

    // Honour the 1-based start row.
    int skip = std::max(0, opt.startRow - 1);
    if (skip > 0 && skip < static_cast<int>(rows.size())) {
        rows.erase(rows.begin(), rows.begin() + skip);
    } else if (skip >= static_cast<int>(rows.size())) {
        rows.clear();
    }

    return rows;
}

// ============================================================================
// NUMBER RECOGNITION
// ============================================================================

// Try to interpret a raw field as a localised number, honouring the configured
// decimal/thousands separators. Returns true and fills 'value' on success.
// Trailing currency symbols / percent signs and surrounding spaces are ignored.
inline bool CSVTryParseNumber(const std::string& raw, const CSVImportOptions& opt,
                              double& value, bool& isPercent) {
    isPercent = false;
    if (!opt.detectNumbers) return false;

    // Strip surrounding whitespace.
    size_t b = raw.find_first_not_of(" \t");
    size_t e = raw.find_last_not_of(" \t");
    if (b == std::string::npos) return false;
    std::string s = raw.substr(b, e - b + 1);
    if (s.empty()) return false;

    // Drop a trailing percent or a leading/trailing currency-ish symbol.
    if (!s.empty() && s.back() == '%') { isPercent = true; s.pop_back(); }
    auto isCurrency = [](unsigned char c) {
        return c == '$' || c == 0xA4 /*latin1 generic*/ ;
    };
    // Strip non-numeric, non-sign, non-separator trailing tokens (e.g. " EUR").
    while (!s.empty() && (s.back() == ' ' || isCurrency((unsigned char)s.back()))) s.pop_back();
    // Multibyte currency suffixes (e.g. " €") are stripped by removing trailing
    // bytes that are part of the field but not digits once spaces are gone.
    while (!s.empty() && (unsigned char)s.back() >= 0x80) s.pop_back();
    while (!s.empty() && s.back() == ' ') s.pop_back();
    if (s.empty()) return false;

    // Rebuild a C-locale number string.
    std::string norm;
    norm.reserve(s.size());
    for (char c : s) {
        if (c == opt.thousandsSeparator) continue;               // drop groupers
        else if (c == opt.decimalSeparator) norm.push_back('.'); // canonical dot
        else norm.push_back(c);
    }

    try {
        size_t pos = 0;
        double v = std::stod(norm, &pos);
        if (pos != norm.size()) return false;  // trailing junk -> not a number
        value = isPercent ? v / 100.0 : v;
        return true;
    } catch (...) {
        return false;
    }
}

// ============================================================================
// AUTO-DETECTION
// ============================================================================

// Inspect a sample of the file and guess the most likely import settings:
// encoding (from BOM), the dominant field separator and the decimal separator.
// 'rawSample' should be the raw (un-decoded) leading bytes of the file.
inline CSVImportOptions CSVDetectOptions(const std::string& rawSample) {
    CSVImportOptions opt;

    // --- Encoding from BOM ---
    auto has = [&](std::initializer_list<unsigned char> bytes) {
        if (rawSample.size() < bytes.size()) return false;
        size_t i = 0;
        for (unsigned char b : bytes)
            if (static_cast<unsigned char>(rawSample[i++]) != b) return false;
        return true;
    };
    if (has({0xEF, 0xBB, 0xBF}))      opt.encoding = CSVImportOptions::Encoding::UTF8;
    else if (has({0xFF, 0xFE}))       opt.encoding = CSVImportOptions::Encoding::UTF16LE;
    else if (has({0xFE, 0xFF}))       opt.encoding = CSVImportOptions::Encoding::UTF16BE;

    std::string text = CSVDecodeToUtf8(rawSample, opt.encoding);

    // --- Collect up to N sample lines (ignoring quoted newlines, which is fine
    //     for a heuristic) ---
    std::vector<std::string> lines;
    {
        std::string line;
        for (char c : text) {
            if (c == '\n') { lines.push_back(line); line.clear(); if (lines.size() >= 25) break; }
            else if (c != '\r') line += c;
        }
        if (!line.empty() && lines.size() < 25) lines.push_back(line);
    }

    // --- Score candidate separators by per-line consistency ---
    const std::array<char, 4> candidates = { ',', ';', '\t', '|' };
    char best = ',';
    double bestScore = -1.0;
    int bestCols = 1;

    auto countOutsideQuotes = [&](const std::string& line, char sep) {
        int n = 0; bool q = false;
        for (char c : line) {
            if (c == '"') q = !q;
            else if (!q && c == sep) ++n;
        }
        return n;
    };

    for (char sep : candidates) {
        // Build per-line field counts (separators + 1) for non-empty lines.
        std::vector<int> counts;
        for (const auto& l : lines) {
            if (l.empty()) continue;
            counts.push_back(countOutsideQuotes(l, sep) + 1);
        }
        if (counts.empty()) continue;

        // Most common column count = mode; score rewards >1 column and
        // consistency across lines.
        std::map<int, int> freq;
        for (int c : counts) freq[c]++;
        int modeCols = 1, modeFreq = 0;
        for (auto& [cols, f] : freq) {
            if (f > modeFreq && cols > 1) { modeFreq = f; modeCols = cols; }
        }
        if (modeCols <= 1) continue;

        double consistency = static_cast<double>(modeFreq) / counts.size();
        double score = consistency * modeCols;  // prefer many consistent columns
        if (score > bestScore) {
            bestScore = score;
            best = sep;
            bestCols = modeCols;
        }
    }

    if (bestScore > 0.0) {
        opt.SetSingleSeparator(best);
    } else {
        // Fall back: comma if present anywhere, else semicolon, else tab.
        if (text.find(',') != std::string::npos) opt.SetSingleSeparator(',');
        else if (text.find(';') != std::string::npos) opt.SetSingleSeparator(';');
        else if (text.find('\t') != std::string::npos) opt.SetSingleSeparator('\t');
        else opt.SetSingleSeparator(',');
    }

    // --- Decimal separator: when the separator is ';' a comma decimal is very
    //     likely (European convention); otherwise assume a dot. ---
    if (opt.HasSeparator(';')) {
        opt.decimalSeparator = ',';
        opt.thousandsSeparator = '.';
    } else {
        opt.decimalSeparator = '.';
        opt.thousandsSeparator = ',';
    }

    (void)bestCols;
    return opt;
}

// Convenience: detect options straight from a file path.
inline bool CSVDetectOptionsFromFile(const std::string& path, CSVImportOptions& out) {
    std::string raw;
    if (!CSVReadFileRaw(path, raw)) return false;
    // A 64 KB sample is plenty for detection while staying cheap on huge files.
    out = CSVDetectOptions(raw.substr(0, std::min<size_t>(raw.size(), 64 * 1024)));
    return true;
}

// Convenience: read + decode + parse a whole file with the given options.
inline bool CSVParseFile(const std::string& path, const CSVImportOptions& opt,
                         CSVGrid& out) {
    std::string raw;
    if (!CSVReadFileRaw(path, raw)) return false;
    out = CSVParse(CSVDecodeToUtf8(raw, opt.encoding), opt);
    return true;
}

} // namespace UltraCanvas
