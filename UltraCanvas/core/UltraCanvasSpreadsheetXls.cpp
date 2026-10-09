// core/UltraCanvasSpreadsheetXls.cpp
// Legacy binary Excel workbook reader (.xls, BIFF5 / BIFF8) - see
// UltraCanvasSpreadsheetXls.h for what it reads and what it leaves out.
//
// A workbook stream is a flat run of records (2-byte type, 2-byte length,
// body). Long records continue in CONTINUE records. The stream opens with the
// workbook globals (fonts, formats, the shared string table, the sheet list,
// defined names), and each worksheet follows as its own BOF..EOF substream,
// located by the stream offset its BOUNDSHEET record gives.
// Version: 1.0.0
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework

#include "UltraCanvasSpreadsheetXls.h"
#include "UltraCanvasCompoundFile.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasTextUtils.h"
#include "HTMLReader/HTMLDocument.h"
#include "HTMLReader/HTMLParser.h"

#include "tinyxml2.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>
#include <set>
#include <utility>

namespace UltraCanvas {

namespace {

// ===== RECORD TYPES =====

enum : uint16_t {
    kFormula = 0x0006, kEof = 0x000A, kExternSheet = 0x0017, kName = 0x0018,
    kDateMode = 0x0022, kFilePass = 0x002F, kFont = 0x0031, kContinue = 0x003C,
    kCodePage = 0x0042, kDefColWidth = 0x0055, kColInfo = 0x007D, kBoundSheet = 0x0085,
    kPalette = 0x0092, kMulRk = 0x00BD, kMulBlank = 0x00BE, kRString = 0x00D6,
    kXf = 0x00E0, kMergeCells = 0x00E5, kSst = 0x00FC, kLabelSst = 0x00FD,
    kSupBook = 0x01AE, kBlank = 0x0201, kNumber = 0x0203, kLabel = 0x0204,
    kBoolErr = 0x0205, kString = 0x0207, kRow = 0x0208, kArray = 0x0221,
    kDefaultRowHeight = 0x0225, kTable = 0x0236, kRk = 0x027E, kFormat = 0x041E,
    kShrFmla = 0x04BC, kBof = 0x0809,
};

// ===== BYTES =====

uint16_t U16(const std::vector<uint8_t>& d, size_t at) {
    return at + 2 <= d.size() ? static_cast<uint16_t>(d[at] | (d[at + 1] << 8)) : 0;
}

uint32_t U32(const std::vector<uint8_t>& d, size_t at) {
    if (at + 4 > d.size()) return 0;
    return static_cast<uint32_t>(d[at]) | (static_cast<uint32_t>(d[at + 1]) << 8) |
           (static_cast<uint32_t>(d[at + 2]) << 16) | (static_cast<uint32_t>(d[at + 3]) << 24);
}

double F64(const std::vector<uint8_t>& d, size_t at) {
    if (at + 8 > d.size()) return 0.0;
    uint64_t bits = 0;
    for (int i = 7; i >= 0; --i) bits = (bits << 8) | d[at + static_cast<size_t>(i)];
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
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

std::string Utf16ToUtf8(const std::vector<uint16_t>& units) {
    std::string out;
    out.reserve(units.size());
    for (size_t i = 0; i < units.size(); ++i) {
        uint32_t cp = units[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < units.size() &&
            units[i + 1] >= 0xDC00 && units[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (units[i + 1] - 0xDC00);
            ++i;
        } else if (cp >= 0xD800 && cp <= 0xDFFF) {
            cp = 0xFFFD;   // an unpaired surrogate
        }
        AppendUtf8(out, cp);
    }
    return out;
}

// The 0x80-0xFF halves of the Windows code pages BIFF5 files are written in
// most often. Anything else is read as Latin-1, which keeps ASCII intact.
uint32_t CodePageToUnicode(uint8_t byte, int codePage) {
    static const uint16_t cp1252[32] = {
        0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
        0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178};
    static const uint16_t cp1250[128] = {
        0x20AC, 0xFFFD, 0x201A, 0xFFFD, 0x201E, 0x2026, 0x2020, 0x2021,
        0xFFFD, 0x2030, 0x0160, 0x2039, 0x015A, 0x0164, 0x017D, 0x0179,
        0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0xFFFD, 0x2122, 0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A,
        0x00A0, 0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6, 0x00A7,
        0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x017B,
        0x00B0, 0x00B1, 0x02DB, 0x0142, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
        0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C,
        0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7,
        0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
        0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7,
        0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
        0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7,
        0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
        0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7,
        0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9};
    static const uint16_t cp1251[64] = {
        0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
        0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
        0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0xFFFD, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
        0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
        0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
        0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
        0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457};
    if (byte < 0x80) return byte;
    switch (codePage) {
        case 1250: return cp1250[byte - 0x80];
        case 1251: return byte >= 0xC0 ? 0x0410 + (byte - 0xC0) : cp1251[byte - 0x80];
        case 1252:
        case 0x8001:   // "Windows ANSI" as Excel 5 sometimes writes it
            return byte < 0xA0 ? cp1252[byte - 0x80] : byte;
        default:
            return byte;   // Latin-1
    }
}

std::string DecodeCodePage(const uint8_t* bytes, size_t count, int codePage) {
    std::string out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) AppendUtf8(out, CodePageToUnicode(bytes[i], codePage));
    return out;
}

// ===== RECORDS =====

struct Record {
    uint16_t type = 0;
    size_t offset = 0;                 // of the record header in the stream
    std::vector<uint8_t> data;         // the body, CONTINUE bodies appended
    std::vector<size_t> continues;     // where in `data` each CONTINUE body begins
};

std::vector<Record> SplitRecords(const std::vector<uint8_t>& stream) {
    std::vector<Record> records;
    size_t pos = 0;
    while (pos + 4 <= stream.size()) {
        const uint16_t type = U16(stream, pos);
        const size_t length = U16(stream, pos + 2);
        const size_t start = pos + 4;
        const size_t end = std::min(stream.size(), start + length);
        if (type == kContinue && !records.empty()) {
            Record& last = records.back();
            last.continues.push_back(last.data.size());
            last.data.insert(last.data.end(), stream.begin() + start, stream.begin() + end);
        } else {
            Record r;
            r.type = type;
            r.offset = pos;
            r.data.assign(stream.begin() + start, stream.begin() + end);
            records.push_back(std::move(r));
        }
        pos = start + length;
    }
    return records;
}

// A read position in a record. Strings are read the BIFF8 way, which knows
// that a string's characters may run on into the next CONTINUE body - and
// that such a body then starts with a fresh "are the characters 16-bit" flag.
class Cursor {
public:
    explicit Cursor(const Record& record, size_t pos = 0) : r_(record), pos_(pos) {}

    size_t Pos() const { return pos_; }
    bool Has(size_t n) const { return pos_ + n <= r_.data.size(); }
    bool AtEnd() const { return pos_ >= r_.data.size(); }
    void Skip(size_t n) { pos_ = std::min(r_.data.size(), pos_ + n); }
    uint8_t U8() { return pos_ < r_.data.size() ? r_.data[pos_++] : (pos_++, 0); }
    uint16_t Word() { const uint16_t v = U16(r_.data, pos_); pos_ += 2; return v; }
    uint32_t DWord() { const uint32_t v = U32(r_.data, pos_); pos_ += 4; return v; }

    // BIFF8 XLUnicodeString / ShortXLUnicodeString (cchBytes = 2 / 1).
    std::string UnicodeString(int cchBytes) {
        const size_t count = cchBytes == 1 ? U8() : Word();
        return UnicodeStringNoCch(count);
    }

    // The flags byte and characters of a BIFF8 string whose length is known.
    std::string UnicodeStringNoCch(size_t count) {
        const uint8_t flags = U8();
        size_t runs = 0, ext = 0;
        if (flags & 0x08) runs = Word();
        if (flags & 0x04) ext = DWord();
        std::string text = Chars(count, (flags & 0x01) != 0);
        Skip(runs * 4 + ext);
        return text;
    }

    // BIFF5 byte string in the workbook's code page.
    std::string ByteString(int cchBytes, int codePage) {
        const size_t count = cchBytes == 1 ? U8() : Word();
        const size_t take = std::min(count, r_.data.size() - std::min(pos_, r_.data.size()));
        std::string text = DecodeCodePage(r_.data.data() + std::min(pos_, r_.data.size()),
                                          take, codePage);
        Skip(count);
        return text;
    }

private:
    const Record& r_;
    size_t pos_;

    bool IsBoundary(size_t at) const {
        return std::binary_search(r_.continues.begin(), r_.continues.end(), at);
    }

    size_t NextBoundary(size_t after) const {
        auto it = std::upper_bound(r_.continues.begin(), r_.continues.end(), after);
        return it == r_.continues.end() ? r_.data.size() : std::min(*it, r_.data.size());
    }

    std::string Chars(size_t count, bool highByte) {
        std::vector<uint16_t> units;
        units.reserve(count);
        size_t remaining = count;
        while (remaining > 0 && pos_ < r_.data.size()) {
            // Character data that starts a CONTINUE body is preceded by its
            // own flags byte (a string's header never is: it is not split).
            if (IsBoundary(pos_)) {
                highByte = (r_.data[pos_] & 0x01) != 0;
                ++pos_;
                continue;
            }
            const size_t limit = NextBoundary(pos_);
            const size_t width = highByte ? 2 : 1;
            const size_t available = (limit - std::min(limit, pos_)) / width;
            if (available == 0) {
                if (limit >= r_.data.size()) break;
                pos_ = limit;
                continue;
            }
            const size_t take = std::min(available, remaining);
            for (size_t i = 0; i < take; ++i) {
                units.push_back(highByte ? U16(r_.data, pos_) : r_.data[pos_]);
                pos_ += width;
            }
            remaining -= take;
        }
        return Utf16ToUtf8(units);
    }
};

// ===== COLOURS =====

// Excel 97's default palette, indices 8-63 (0-7 repeat the first eight).
const uint32_t kDefaultPalette[56] = {
    0x000000, 0xFFFFFF, 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF,
    0x800000, 0x008000, 0x000080, 0x808000, 0x800080, 0x008080, 0xC0C0C0, 0x808080,
    0x9999FF, 0x993366, 0xFFFFCC, 0xCCFFFF, 0x660066, 0xFF8080, 0x0066CC, 0xCCCCFF,
    0x000080, 0xFF00FF, 0xFFFF00, 0x00FFFF, 0x800080, 0x800000, 0x008080, 0x0000FF,
    0x00CCFF, 0xCCFFFF, 0xCCFFCC, 0xFFFF99, 0x99CCFF, 0xFF99CC, 0xCC99FF, 0xFFCC99,
    0x3366FF, 0x33CCCC, 0x99CC00, 0xFFCC00, 0xFF9900, 0xFF6600, 0x666699, 0x969696,
    0x003366, 0x339966, 0x003300, 0x333300, 0x993300, 0x993366, 0x333399, 0x333333};

Color RgbColor(uint32_t rgb) {
    return Color(static_cast<uint8_t>((rgb >> 16) & 0xFF), static_cast<uint8_t>((rgb >> 8) & 0xFF),
                 static_cast<uint8_t>(rgb & 0xFF));
}

// ===== NUMBER FORMATS =====

const char* BuiltInFormatCode(int id) {
    switch (id) {
        case 0: return "General";
        case 1: return "0";
        case 2: return "0.00";
        case 3: return "#,##0";
        case 4: return "#,##0.00";
        case 5: return "\"$\"#,##0_);(\"$\"#,##0)";
        case 6: return "\"$\"#,##0_);[Red](\"$\"#,##0)";
        case 7: return "\"$\"#,##0.00_);(\"$\"#,##0.00)";
        case 8: return "\"$\"#,##0.00_);[Red](\"$\"#,##0.00)";
        case 9: return "0%";
        case 10: return "0.00%";
        case 11: return "0.00E+00";
        case 12: return "# ?/?";
        case 13: return "# ?\?/?\?";
        case 14: return "m/d/yy";
        case 15: return "d-mmm-yy";
        case 16: return "d-mmm";
        case 17: return "mmm-yy";
        case 18: return "h:mm AM/PM";
        case 19: return "h:mm:ss AM/PM";
        case 20: return "h:mm";
        case 21: return "h:mm:ss";
        case 22: return "m/d/yy h:mm";
        case 37: return "#,##0_);(#,##0)";
        case 38: return "#,##0_);[Red](#,##0)";
        case 39: return "#,##0.00_);(#,##0.00)";
        case 40: return "#,##0.00_);[Red](#,##0.00)";
        case 41: return "_(* #,##0_);_(* (#,##0);_(* \"-\"_);_(@_)";
        case 42: return "_(\"$\"* #,##0_);_(\"$\"* (#,##0);_(\"$\"* \"-\"_);_(@_)";
        case 43: return "_(* #,##0.00_);_(* (#,##0.00);_(* \"-\"??_);_(@_)";
        case 44: return "_(\"$\"* #,##0.00_);_(\"$\"* (#,##0.00);_(\"$\"* \"-\"??_);_(@_)";
        case 45: return "mm:ss";
        case 46: return "[h]:mm:ss";
        case 47: return "mm:ss.0";
        case 48: return "##0.0E+0";
        case 49: return "@";
        default: return "General";
    }
}

// ===== FORMULA FUNCTIONS =====

// Excel's built-in function table, by the index a formula token stores.
// minArgs == maxArgs marks a function stored with a fixed argument count
// (tFunc, no count in the token).
struct FunctionInfo {
    uint16_t index;
    const char* name;
    uint8_t minArgs;
    uint8_t maxArgs;
};

const FunctionInfo kFunctions[] = {
    {0, "COUNT", 0, 30}, {1, "IF", 2, 3}, {2, "ISNA", 1, 1}, {3, "ISERROR", 1, 1},
    {4, "SUM", 0, 30}, {5, "AVERAGE", 1, 30}, {6, "MIN", 1, 30}, {7, "MAX", 1, 30},
    {8, "ROW", 0, 1}, {9, "COLUMN", 0, 1}, {10, "NA", 0, 0}, {11, "NPV", 2, 30},
    {12, "STDEV", 1, 30}, {13, "DOLLAR", 1, 2}, {14, "FIXED", 2, 3}, {15, "SIN", 1, 1},
    {16, "COS", 1, 1}, {17, "TAN", 1, 1}, {18, "ATAN", 1, 1}, {19, "PI", 0, 0},
    {20, "SQRT", 1, 1}, {21, "EXP", 1, 1}, {22, "LN", 1, 1}, {23, "LOG10", 1, 1},
    {24, "ABS", 1, 1}, {25, "INT", 1, 1}, {26, "SIGN", 1, 1}, {27, "ROUND", 2, 2},
    {28, "LOOKUP", 2, 3}, {29, "INDEX", 2, 4}, {30, "REPT", 2, 2}, {31, "MID", 3, 3},
    {32, "LEN", 1, 1}, {33, "VALUE", 1, 1}, {34, "TRUE", 0, 0}, {35, "FALSE", 0, 0},
    {36, "AND", 1, 30}, {37, "OR", 1, 30}, {38, "NOT", 1, 1}, {39, "MOD", 2, 2},
    {40, "DCOUNT", 3, 3}, {41, "DSUM", 3, 3}, {42, "DAVERAGE", 3, 3}, {43, "DMIN", 3, 3},
    {44, "DMAX", 3, 3}, {45, "DSTDEV", 3, 3}, {46, "VAR", 1, 30}, {47, "DVAR", 3, 3},
    {48, "TEXT", 2, 2}, {49, "LINEST", 1, 4}, {50, "TREND", 1, 4}, {51, "LOGEST", 1, 4},
    {52, "GROWTH", 1, 4}, {56, "PV", 3, 5}, {57, "FV", 3, 5}, {58, "NPER", 3, 5},
    {59, "PMT", 3, 5}, {60, "RATE", 3, 6}, {61, "MIRR", 3, 3}, {62, "IRR", 1, 2},
    {63, "RAND", 0, 0}, {64, "MATCH", 2, 3}, {65, "DATE", 3, 3}, {66, "TIME", 3, 3},
    {67, "DAY", 1, 1}, {68, "MONTH", 1, 1}, {69, "YEAR", 1, 1}, {70, "WEEKDAY", 1, 2},
    {71, "HOUR", 1, 1}, {72, "MINUTE", 1, 1}, {73, "SECOND", 1, 1}, {74, "NOW", 0, 0},
    {75, "AREAS", 1, 1}, {76, "ROWS", 1, 1}, {77, "COLUMNS", 1, 1}, {78, "OFFSET", 3, 5},
    {82, "SEARCH", 2, 3}, {83, "TRANSPOSE", 1, 1}, {86, "TYPE", 1, 1}, {97, "ATAN2", 2, 2},
    {98, "ASIN", 1, 1}, {99, "ACOS", 1, 1}, {100, "CHOOSE", 2, 30}, {101, "HLOOKUP", 3, 4},
    {102, "VLOOKUP", 3, 4}, {105, "ISREF", 1, 1}, {109, "LOG", 1, 2}, {111, "CHAR", 1, 1},
    {112, "LOWER", 1, 1}, {113, "UPPER", 1, 1}, {114, "PROPER", 1, 1}, {115, "LEFT", 1, 2},
    {116, "RIGHT", 1, 2}, {117, "EXACT", 2, 2}, {118, "TRIM", 1, 1}, {119, "REPLACE", 4, 4},
    {120, "SUBSTITUTE", 3, 4}, {121, "CODE", 1, 1}, {124, "FIND", 2, 3}, {125, "CELL", 1, 2},
    {126, "ISERR", 1, 1}, {127, "ISTEXT", 1, 1}, {128, "ISNUMBER", 1, 1},
    {129, "ISBLANK", 1, 1}, {130, "T", 1, 1}, {131, "N", 1, 1}, {140, "DATEVALUE", 1, 1},
    {141, "TIMEVALUE", 1, 1}, {142, "SLN", 3, 3}, {143, "SYD", 4, 4}, {144, "DDB", 4, 5},
    {148, "INDIRECT", 1, 2}, {162, "CLEAN", 1, 1}, {163, "MDETERM", 1, 1},
    {164, "MINVERSE", 1, 1}, {165, "MMULT", 2, 2}, {167, "IPMT", 4, 6}, {168, "PPMT", 4, 6},
    {169, "COUNTA", 0, 30}, {183, "PRODUCT", 0, 30}, {184, "FACT", 1, 1},
    {189, "DPRODUCT", 3, 3}, {190, "ISNONTEXT", 1, 1}, {193, "STDEVP", 1, 30},
    {194, "VARP", 1, 30}, {195, "DSTDEVP", 3, 3}, {196, "DVARP", 3, 3}, {197, "TRUNC", 1, 2},
    {198, "ISLOGICAL", 1, 1}, {199, "DCOUNTA", 3, 3}, {204, "USDOLLAR", 1, 2},
    {205, "FINDB", 2, 3}, {206, "SEARCHB", 2, 3}, {207, "REPLACEB", 4, 4},
    {208, "LEFTB", 1, 2}, {209, "RIGHTB", 1, 2}, {210, "MIDB", 3, 3}, {211, "LENB", 1, 1},
    {212, "ROUNDUP", 2, 2}, {213, "ROUNDDOWN", 2, 2}, {214, "ASC", 1, 1}, {215, "DBCS", 1, 1},
    {216, "RANK", 2, 3}, {219, "ADDRESS", 2, 5}, {220, "DAYS360", 2, 3}, {221, "TODAY", 0, 0},
    {222, "VDB", 5, 7}, {227, "MEDIAN", 1, 30}, {228, "SUMPRODUCT", 1, 30},
    {229, "SINH", 1, 1}, {230, "COSH", 1, 1}, {231, "TANH", 1, 1}, {232, "ASINH", 1, 1},
    {233, "ACOSH", 1, 1}, {234, "ATANH", 1, 1}, {235, "DGET", 3, 3}, {244, "INFO", 1, 1},
    {247, "DB", 4, 5}, {252, "FREQUENCY", 2, 2}, {261, "ERROR.TYPE", 1, 1},
    {269, "AVEDEV", 1, 30}, {270, "BETADIST", 3, 5}, {271, "GAMMALN", 1, 1},
    {272, "BETAINV", 3, 5}, {273, "BINOMDIST", 4, 4}, {274, "CHIDIST", 2, 2},
    {275, "CHIINV", 2, 2}, {276, "COMBIN", 2, 2}, {277, "CONFIDENCE", 3, 3},
    {278, "CRITBINOM", 3, 3}, {279, "EVEN", 1, 1}, {280, "EXPONDIST", 3, 3},
    {281, "FDIST", 3, 3}, {282, "FINV", 3, 3}, {283, "FISHER", 1, 1},
    {284, "FISHERINV", 1, 1}, {285, "FLOOR", 2, 2}, {286, "GAMMADIST", 4, 4},
    {287, "GAMMAINV", 3, 3}, {288, "CEILING", 2, 2}, {289, "HYPGEOMDIST", 4, 4},
    {290, "LOGNORMDIST", 3, 3}, {291, "LOGINV", 3, 3}, {292, "NEGBINOMDIST", 3, 3},
    {293, "NORMDIST", 4, 4}, {294, "NORMSDIST", 1, 1}, {295, "NORMINV", 3, 3},
    {296, "NORMSINV", 1, 1}, {297, "STANDARDIZE", 3, 3}, {298, "ODD", 1, 1},
    {299, "PERMUT", 2, 2}, {300, "POISSON", 3, 3}, {301, "TDIST", 3, 3},
    {302, "WEIBULL", 4, 4}, {303, "SUMXMY2", 2, 2}, {304, "SUMX2MY2", 2, 2},
    {305, "SUMX2PY2", 2, 2}, {306, "CHITEST", 2, 2}, {307, "CORREL", 2, 2},
    {308, "COVAR", 2, 2}, {309, "FORECAST", 3, 3}, {310, "FTEST", 2, 2},
    {311, "INTERCEPT", 2, 2}, {312, "PEARSON", 2, 2}, {313, "RSQ", 2, 2},
    {314, "STEYX", 2, 2}, {315, "SLOPE", 2, 2}, {316, "TTEST", 4, 4}, {317, "PROB", 3, 4},
    {318, "DEVSQ", 1, 30}, {319, "GEOMEAN", 1, 30}, {320, "HARMEAN", 1, 30},
    {321, "SUMSQ", 0, 30}, {322, "KURT", 1, 30}, {323, "SKEW", 1, 30}, {324, "ZTEST", 2, 3},
    {325, "LARGE", 2, 2}, {326, "SMALL", 2, 2}, {327, "QUARTILE", 2, 2},
    {328, "PERCENTILE", 2, 2}, {329, "PERCENTRANK", 2, 3}, {330, "MODE", 1, 30},
    {331, "TRIMMEAN", 2, 2}, {332, "TINV", 2, 2}, {336, "CONCATENATE", 0, 30},
    {337, "POWER", 2, 2}, {342, "RADIANS", 1, 1}, {343, "DEGREES", 1, 1},
    {344, "SUBTOTAL", 2, 30}, {345, "SUMIF", 2, 3}, {346, "COUNTIF", 2, 2},
    {347, "COUNTBLANK", 1, 1}, {350, "ISPMT", 4, 4}, {351, "DATEDIF", 3, 3},
    {352, "DATESTRING", 1, 1}, {353, "NUMBERSTRING", 2, 2}, {354, "ROMAN", 1, 2},
    {358, "GETPIVOTDATA", 2, 30}, {359, "HYPERLINK", 1, 2}, {360, "PHONETIC", 1, 1},
    {361, "AVERAGEA", 1, 30}, {362, "MAXA", 1, 30}, {363, "MINA", 1, 30},
    {364, "STDEVPA", 1, 30}, {365, "VARPA", 1, 30}, {366, "STDEVA", 1, 30},
    {367, "VARA", 1, 30}, {368, "BAHTTEXT", 1, 1},
};

const FunctionInfo* FindFunction(uint16_t index) {
    for (const FunctionInfo& f : kFunctions) {
        if (f.index == index) return &f;
    }
    return nullptr;
}

// ===== A1 REFERENCES =====

std::string ColumnName(int col) {
    std::string letters;
    ++col;
    while (col > 0) {
        const int rem = (col - 1) % 26;
        letters.insert(letters.begin(), static_cast<char>('A' + rem));
        col = (col - 1) / 26;
    }
    return letters;
}

std::string CellName(int row, int col, bool rowRelative, bool colRelative) {
    std::string out;
    if (!colRelative) out.push_back('$');
    out += ColumnName(col);
    if (!rowRelative) out.push_back('$');
    out += std::to_string(row + 1);
    return out;
}

// 'Sheet Name'. as the engine reads a sheet-qualified reference. False for a
// name its reference syntax cannot carry: it splits sheet from cell at the
// first '.', and does not undo a doubled quote.
bool QuotedSheet(const std::string& name, std::string& out) {
    if (name.empty() || name.find_first_of(".'") != std::string::npos) return false;
    out = "'" + name + "'.";
    return true;
}

// The shortest dot-decimal text that reads back as exactly `value`.
std::string NumberLiteral(double value) {
    for (int precision = 15; precision <= 17; ++precision) {
        std::string text = FormatFloatClassic(value, precision);
        double back = 0.0;
        if (TryParseFloat(text, back) && back == value) return text;
    }
    return FormatFloatClassic(value, 17);
}

// ===== THE PARSER =====

struct BoundSheet {
    std::string name;
    uint32_t offset = 0;
    uint8_t kind = 0;     // 0 worksheet, 1 macro sheet, 2 chart, 6 VB module
    bool hidden = false;
    int modelIndex = -1;  // index into XlsWorkbook::sheets, -1 when not read
};

struct ExternSheet {
    int supBook = 0;
    int firstSheet = 0;
    int lastSheet = 0;
};

struct SharedFormula {
    int firstRow = 0, lastRow = 0, firstCol = 0, lastCol = 0;
    std::vector<uint8_t> tokens;
};

class BiffParser {
public:
    BiffParser(XlsWorkbook& out, const XlsReadOptions& options) : wb_(out), opt_(options) {}

    bool Parse(const std::vector<uint8_t>& stream, int streamBiff, std::string& error) {
        records_ = SplitRecords(stream);
        if (records_.empty() || records_[0].type != kBof) {
            error = "The workbook stream does not start with a BOF record";
            return false;
        }
        const uint16_t version = U16(records_[0].data, 0);
        biff_ = version == 0x0600 ? 8 : version == 0x0500 ? 5 : streamBiff;
        wb_.biffVersion = biff_;
        if (biff_ == 5) wb_.codePage = 1252;

        size_t index = 1;
        if (!ParseGlobals(index, error)) return false;
        ParseNames();
        ParseSheets(index);
        return true;
    }

private:
    XlsWorkbook& wb_;
    const XlsReadOptions& opt_;
    std::vector<Record> records_;
    int biff_ = 8;
    std::vector<std::string> sst_;
    std::vector<XlsFont> fonts_;
    std::vector<uint16_t> fontColors_;   // each font's palette index, resolved at the end
    std::vector<uint32_t> palette_ = std::vector<uint32_t>(std::begin(kDefaultPalette),
                                                           std::end(kDefaultPalette));
    std::map<int, std::string> formatCodes_;
    struct RawXf { std::vector<uint8_t> data; };
    std::vector<RawXf> xfs_;
    std::vector<BoundSheet> sheets_;
    std::vector<ExternSheet> externSheets_;
    std::vector<bool> supBookIsSelf_;
    std::vector<const Record*> nameRecords_;
    std::vector<int> nameModelIndex_;   // NAME record -> index in wb_.names, -1 if not kept

    // ----- strings -----

    std::string ReadString(Cursor& c, int cchBytes) {
        return biff_ >= 8 ? c.UnicodeString(cchBytes) : c.ByteString(cchBytes, wb_.codePage);
    }

    // ----- colours -----

    Color PaletteColor(int icv, const Color& automatic) const {
        if (icv >= 0 && icv < 8) return RgbColor(kDefaultPalette[icv]);
        if (icv >= 8 && icv < 64) return RgbColor(palette_[static_cast<size_t>(icv - 8)]);
        return automatic;   // 64 window text / 65 window background / 0x7FFF auto
    }

    // ----- globals -----

    bool ParseGlobals(size_t& index, std::string& error) {
        for (; index < records_.size(); ++index) {
            const Record& r = records_[index];
            switch (r.type) {
                case kEof:
                    ++index;
                    FinishFormats();
                    return true;
                case kFilePass:
                    error = "The workbook is password-protected (encrypted); "
                            "it cannot be read without the password";
                    return false;
                case kCodePage:
                    if (biff_ < 8) wb_.codePage = U16(r.data, 0);
                    else wb_.codePage = 1200;
                    break;
                case kDateMode:
                    wb_.date1904 = U16(r.data, 0) != 0;
                    break;
                case kFont:
                    ParseFont(r);
                    break;
                case kFormat: {
                    Cursor c(r);
                    const int id = c.Word();
                    formatCodes_[id] = ReadString(c, biff_ >= 8 ? 2 : 1);
                    break;
                }
                case kXf:
                    xfs_.push_back({r.data});
                    break;
                case kPalette: {
                    const size_t count = std::min<size_t>(U16(r.data, 0), 56);
                    for (size_t i = 0; i < count; ++i) {
                        const size_t at = 2 + i * 4;
                        if (at + 3 > r.data.size()) break;
                        palette_[i] = (static_cast<uint32_t>(r.data[at]) << 16) |
                                      (static_cast<uint32_t>(r.data[at + 1]) << 8) | r.data[at + 2];
                    }
                    break;
                }
                case kBoundSheet: {
                    Cursor c(r);
                    BoundSheet s;
                    s.offset = c.DWord();
                    s.hidden = (c.U8() & 0x03) != 0;
                    s.kind = c.U8();
                    s.name = ReadString(c, 1);
                    sheets_.push_back(std::move(s));
                    break;
                }
                case kSst:
                    ParseSst(r);
                    break;
                case kSupBook:
                    // ctab, then cch: 0x0401 marks the workbook itself.
                    supBookIsSelf_.push_back(r.data.size() >= 4 && U16(r.data, 2) == 0x0401);
                    break;
                case kExternSheet:
                    if (biff_ >= 8) {
                        const size_t count = U16(r.data, 0);
                        for (size_t i = 0; i < count; ++i) {
                            const size_t at = 2 + i * 6;
                            if (at + 6 > r.data.size()) break;
                            externSheets_.push_back({U16(r.data, at), U16(r.data, at + 2),
                                                     U16(r.data, at + 4)});
                        }
                    }
                    break;
                case kName:
                    nameRecords_.push_back(&r);
                    break;
                case kBof:
                    // A worksheet without the globals' EOF before it: the
                    // globals end here.
                    FinishFormats();
                    return true;
                default:
                    break;
            }
        }
        FinishFormats();
        return true;
    }

    void ParseFont(const Record& r) {
        XlsFont f;
        f.sizePoints = U16(r.data, 0) / 20.0;
        const uint16_t grbit = U16(r.data, 2);
        f.italic = (grbit & 0x02) != 0;
        f.strikethrough = (grbit & 0x08) != 0;
        const uint16_t icv = U16(r.data, 4);
        f.automaticColor = icv >= 64;
        fontColors_.push_back(icv);
        f.bold = U16(r.data, 6) >= 600;
        const uint16_t sss = U16(r.data, 8);
        f.superscript = sss == 1;
        f.subscript = sss == 2;
        const uint8_t uls = r.data.size() > 10 ? r.data[10] : 0;
        f.underline = (uls == 0x02 || uls == 0x22) ? 2 : (uls ? 1 : 0);
        Cursor c(r, 14);
        f.name = ReadString(c, 1);
        fonts_.push_back(std::move(f));
    }

    // Colours are stored as palette indices, and the PALETTE record comes
    // after the fonts and formats that use it: resolve them at the end.
    void FinishFormats() {
        wb_.fonts = fonts_;
        for (size_t i = 0; i < wb_.fonts.size() && i < fontColors_.size(); ++i) {
            wb_.fonts[i].color = PaletteColor(fontColors_[i], Colors::Black);
        }
        wb_.formats.clear();
        for (const RawXf& raw : xfs_) wb_.formats.push_back(DecodeXf(raw.data));
    }

    XlsCellFormat DecodeXf(const std::vector<uint8_t>& d) const {
        XlsCellFormat f;
        int ifnt = U16(d, 0);
        if (ifnt >= 4) --ifnt;   // font index 4 is never stored
        f.font = (ifnt >= 0 && ifnt < static_cast<int>(fonts_.size())) ? ifnt : 0;
        f.numberFormatId = U16(d, 2);
        auto code = formatCodes_.find(f.numberFormatId);
        f.numberFormatCode = code != formatCodes_.end() ? code->second
                                                        : BuiltInFormatCode(f.numberFormatId);
        const uint16_t typeProt = U16(d, 4);
        f.locked = (typeProt & 0x0001) != 0;
        f.formulaHidden = (typeProt & 0x0002) != 0;
        const uint8_t align = d.size() > 6 ? d[6] : 0;
        f.horizontalAlign = align & 0x07;
        f.wrapText = (align & 0x08) != 0;
        f.verticalAlign = (align >> 4) & 0x07;

        auto line = [&](int style, int icv) {
            XlsBorderLine l;
            l.style = style;
            l.color = PaletteColor(icv, Colors::Black);
            return l;
        };
        if (biff_ >= 8) {
            const int trot = d.size() > 7 ? d[7] : 0;
            f.rotation = trot <= 90 ? trot : trot <= 180 ? 90 - trot : 0;
            const uint8_t indent = d.size() > 8 ? d[8] : 0;
            f.indent = indent & 0x0F;
            f.shrinkToFit = (indent & 0x10) != 0;
            const uint32_t b1 = U32(d, 10), b2 = U32(d, 14);
            const uint16_t b3 = U16(d, 18);
            f.left = line(b1 & 0x0F, (b1 >> 16) & 0x7F);
            f.right = line((b1 >> 4) & 0x0F, (b1 >> 23) & 0x7F);
            f.top = line((b1 >> 8) & 0x0F, b2 & 0x7F);
            f.bottom = line((b1 >> 12) & 0x0F, (b2 >> 7) & 0x7F);
            f.fillPattern = static_cast<int>((b2 >> 26) & 0x3F);
            f.patternColor = PaletteColor(b3 & 0x7F, Colors::Black);
            f.backgroundColor = PaletteColor((b3 >> 7) & 0x7F, Colors::White);
        } else {
            const uint8_t orient = d.size() > 7 ? (d[7] & 0x03) : 0;
            f.rotation = orient == 2 ? 90 : orient == 3 ? -90 : 0;
            const uint32_t b1 = U32(d, 8), b2 = U32(d, 12);
            f.patternColor = PaletteColor(b1 & 0x7F, Colors::Black);
            f.backgroundColor = PaletteColor((b1 >> 7) & 0x7F, Colors::White);
            f.fillPattern = static_cast<int>((b1 >> 16) & 0x3F);
            f.bottom = line((b1 >> 22) & 0x07, (b1 >> 25) & 0x7F);
            f.top = line(b2 & 0x07, (b2 >> 9) & 0x7F);
            f.left = line((b2 >> 3) & 0x07, (b2 >> 16) & 0x7F);
            f.right = line((b2 >> 6) & 0x07, (b2 >> 23) & 0x7F);
        }
        return f;
    }

    void ParseSst(const Record& r) {
        Cursor c(r);
        c.DWord();                        // total references
        const uint32_t unique = c.DWord();
        sst_.reserve(std::min<uint32_t>(unique, 1u << 20));
        for (uint32_t i = 0; i < unique && !c.AtEnd(); ++i) {
            sst_.push_back(c.UnicodeString(2));
        }
    }

    // ----- defined names -----

    static const char* BuiltInName(uint8_t code) {
        static const char* const names[] = {
            "Consolidate_Area", "Auto_Open", "Auto_Close", "Extract", "Database", "Criteria",
            "Print_Area", "Print_Titles", "Recorder", "Data_Form", "Auto_Activate",
            "Auto_Deactivate", "Sheet_Title", "_FilterDatabase"};
        return code < sizeof(names) / sizeof(names[0]) ? names[code] : nullptr;
    }

    void ParseNames() {
        nameModelIndex_.assign(nameRecords_.size(), -1);
        // Sheets must have their model indices before names are scoped.
        AssignModelIndices();
        for (size_t i = 0; i < nameRecords_.size(); ++i) {
            const Record& r = *nameRecords_[i];
            if (r.data.size() < 14) continue;
            const uint16_t grbit = U16(r.data, 0);
            const size_t cch = r.data[3];
            const size_t cce = U16(r.data, 4);
            const int itab = U16(r.data, 8);
            XlsDefinedName n;
            n.builtIn = (grbit & 0x0020) != 0;
            Cursor c(r, 14);
            std::string name = biff_ >= 8 ? c.UnicodeStringNoCch(cch)
                                          : DecodeCodePage(r.data.data() + 14,
                                                           std::min(cch, r.data.size() - 14),
                                                           wb_.codePage);
            if (biff_ < 8) c.Skip(cch);
            if (n.builtIn) {
                const char* builtIn = name.empty() ? nullptr
                                                   : BuiltInName(static_cast<uint8_t>(name[0]));
                n.name = builtIn ? builtIn : name;
            } else {
                n.name = name;
            }
            if (n.name.empty()) continue;
            if (itab > 0 && itab <= static_cast<int>(sheets_.size())) {
                n.sheetScope = sheets_[static_cast<size_t>(itab - 1)].modelIndex;
                if (n.sheetScope < 0) continue;   // scoped to a sheet that is not read
            }
            const size_t start = c.Pos();
            if (start + cce <= r.data.size() && cce > 0) {
                std::vector<uint8_t> tokens(r.data.begin() + static_cast<std::ptrdiff_t>(start),
                                            r.data.begin() + static_cast<std::ptrdiff_t>(start + cce));
                DecodeNameTarget(tokens, n);
            }
            nameModelIndex_[i] = static_cast<int>(wb_.names.size());
            wb_.names.push_back(std::move(n));
        }
    }

    // A name that is one 3-D reference or area on one worksheet becomes a
    // plain range the engine can hold as a named range.
    void DecodeNameTarget(const std::vector<uint8_t>& tokens, XlsDefinedName& n) {
        std::string text;
        if (opt_.translateFormulas && Decompile(tokens, 0, 0, false, text)) n.formula = text;
        if (biff_ < 8 || tokens.empty()) return;
        const uint8_t ptg = tokens[0];
        const uint8_t base = static_cast<uint8_t>((ptg & 0x1F) | 0x20);
        const bool area = base == 0x3B;
        if ((base != 0x3A && base != 0x3B) || tokens.size() != static_cast<size_t>(area ? 11 : 7))
            return;
        int sheet = -1;
        if (!SheetOfExtern(U16(tokens, 1), sheet)) return;
        n.refSheet = sheets_[static_cast<size_t>(sheet)].modelIndex;
        if (n.refSheet < 0) return;
        if (area) {
            n.firstRow = U16(tokens, 3);
            n.lastRow = U16(tokens, 5);
            n.firstColumn = U16(tokens, 7) & 0x00FF;
            n.lastColumn = U16(tokens, 9) & 0x00FF;
        } else {
            n.firstRow = n.lastRow = U16(tokens, 3);
            n.firstColumn = n.lastColumn = U16(tokens, 5) & 0x00FF;
        }
    }

    // ----- sheets -----

    void AssignModelIndices() {
        int next = 0;
        for (BoundSheet& s : sheets_) {
            s.modelIndex = -1;
            if (s.kind != 0) continue;
            if (opt_.maxSheets >= 0 && next >= opt_.maxSheets) continue;
            s.modelIndex = next++;
        }
    }

    void ParseSheets(size_t firstAfterGlobals) {
        std::map<size_t, size_t> bofAt;   // stream offset -> record index
        for (size_t i = firstAfterGlobals; i < records_.size(); ++i) {
            if (records_[i].type == kBof) bofAt[records_[i].offset] = i;
        }
        // Substreams in stream order, for files whose BOUNDSHEET offsets are
        // wrong (some writers leave them zero): the n-th sheet is then the
        // n-th substream.
        std::vector<size_t> substreams;
        int depth = 0;
        for (size_t i = firstAfterGlobals; i < records_.size(); ++i) {
            if (records_[i].type == kBof) {
                if (depth == 0) substreams.push_back(i);
                ++depth;
            } else if (records_[i].type == kEof && depth > 0) {
                --depth;
            }
        }
        wb_.sheets.clear();
        for (size_t s = 0; s < sheets_.size(); ++s) {
            const BoundSheet& b = sheets_[s];
            if (b.modelIndex < 0) continue;
            size_t start = SIZE_MAX;
            auto it = bofAt.find(b.offset);
            if (it != bofAt.end()) start = it->second;
            else if (s < substreams.size()) start = substreams[s];
            XlsSheet sheet;
            sheet.name = b.name;
            sheet.hidden = b.hidden;
            if (start != SIZE_MAX) ParseSheet(start, sheet);
            wb_.sheets.push_back(std::move(sheet));
        }
    }

    bool KeepCell(int row, int col) const {
        return (opt_.maxRows < 0 || row < opt_.maxRows) &&
               (opt_.maxColumns < 0 || col < opt_.maxColumns);
    }

    XlsCell& AddCell(XlsSheet& sheet, int row, int col, int xf) {
        XlsCell cell;
        cell.row = row;
        cell.col = col;
        cell.format = (xf >= 0 && xf < static_cast<int>(wb_.formats.size())) ? xf : -1;
        sheet.cells.push_back(std::move(cell));
        return sheet.cells.back();
    }

    static double DecodeRk(uint32_t rk) {
        double value;
        if (rk & 0x02) {
            value = static_cast<double>(static_cast<int32_t>(rk) >> 2);
        } else {
            const uint64_t bits = static_cast<uint64_t>(rk & 0xFFFFFFFCu) << 32;
            std::memcpy(&value, &bits, sizeof(value));
        }
        return (rk & 0x01) ? value / 100.0 : value;
    }

    void ParseSheet(size_t start, XlsSheet& sheet) {
        size_t end = start + 1;
        int depth = 1;
        for (; end < records_.size(); ++end) {
            if (records_[end].type == kBof) ++depth;
            else if (records_[end].type == kEof && --depth == 0) break;
        }

        // Shared formulas, keyed by the cell whose FORMULA record precedes
        // the SHRFMLA (the cell every PtgExp of the range points at).
        std::map<std::pair<int, int>, SharedFormula> shared;
        for (size_t i = start + 1; i < end; ++i) {
            if (records_[i].type != kShrFmla || records_[i - 1].type != kFormula) continue;
            const Record& f = records_[i - 1];
            const Record& r = records_[i];
            if (r.data.size() < 10) continue;
            SharedFormula sf;
            sf.firstRow = U16(r.data, 0);
            sf.lastRow = U16(r.data, 2);
            sf.firstCol = r.data[4];
            sf.lastCol = r.data[5];
            const size_t cce = U16(r.data, 8);
            if (10 + cce > r.data.size()) continue;
            sf.tokens.assign(r.data.begin() + 10, r.data.begin() + 10 + static_cast<std::ptrdiff_t>(cce));
            shared[{U16(f.data, 0), U16(f.data, 2)}] = std::move(sf);
        }

        int nestedDepth = 0;
        int pendingString = -1;   // index of a cell waiting for its STRING record
        for (size_t i = start + 1; i < end; ++i) {
            const Record& r = records_[i];
            if (nestedDepth > 0) {   // an embedded chart's substream
                if (r.type == kBof) ++nestedDepth;
                else if (r.type == kEof) --nestedDepth;
                continue;
            }
            const std::vector<uint8_t>& d = r.data;
            switch (r.type) {
                case kBof:
                    ++nestedDepth;
                    break;
                case kDefColWidth:
                    sheet.defaultColumnWidthChars = U16(d, 0);
                    break;
                case kDefaultRowHeight:
                    if (d.size() >= 4) sheet.defaultRowHeightPoints = U16(d, 2) / 20.0;
                    break;
                case kColInfo: {
                    if (d.size() < 10) break;
                    XlsColumnInfo c;
                    c.firstColumn = U16(d, 0);
                    c.lastColumn = U16(d, 2);
                    c.widthChars = U16(d, 4) / 256.0;
                    c.format = U16(d, 6);
                    const uint16_t flags = U16(d, 8);
                    c.hidden = (flags & 0x0001) != 0;
                    // fUserSet, or not fitted to the content (fBestFit clear).
                    c.customWidth = (flags & 0x0002) != 0 || (flags & 0x0004) == 0;
                    if (opt_.maxColumns >= 0 && c.firstColumn >= opt_.maxColumns) break;
                    sheet.columns.push_back(c);
                    break;
                }
                case kRow: {
                    if (d.size() < 16) break;
                    XlsRowInfo row;
                    row.row = U16(d, 0);
                    row.heightPoints = (U16(d, 6) & 0x7FFF) / 20.0;
                    const uint8_t flags = d[12];
                    row.hidden = (flags & 0x20) != 0;
                    row.customHeight = (flags & 0x40) != 0;
                    if (opt_.maxRows >= 0 && row.row >= opt_.maxRows) break;
                    if (row.hidden || row.customHeight) sheet.rows.push_back(row);
                    break;
                }
                case kMergeCells: {
                    const size_t count = U16(d, 0);
                    for (size_t k = 0; k < count; ++k) {
                        const size_t at = 2 + k * 8;
                        if (at + 8 > d.size()) break;
                        XlsMergedRange m;
                        m.firstRow = U16(d, at);
                        m.lastRow = U16(d, at + 2);
                        m.firstColumn = U16(d, at + 4);
                        m.lastColumn = U16(d, at + 6);
                        if (KeepCell(m.firstRow, m.firstColumn)) sheet.merges.push_back(m);
                    }
                    break;
                }
                case kLabelSst: {
                    const int row = U16(d, 0), col = U16(d, 2);
                    if (!KeepCell(row, col)) break;
                    XlsCell& cell = AddCell(sheet, row, col, U16(d, 4));
                    const uint32_t isst = U32(d, 6);
                    cell.type = XlsValueType::Text;
                    if (isst < sst_.size()) cell.text = sst_[isst];
                    break;
                }
                case kLabel:
                case kRString: {
                    const int row = U16(d, 0), col = U16(d, 2);
                    if (!KeepCell(row, col)) break;
                    XlsCell& cell = AddCell(sheet, row, col, U16(d, 4));
                    Cursor c(r, 6);
                    cell.type = XlsValueType::Text;
                    cell.text = ReadString(c, 2);
                    break;
                }
                case kNumber: {
                    const int row = U16(d, 0), col = U16(d, 2);
                    if (!KeepCell(row, col)) break;
                    XlsCell& cell = AddCell(sheet, row, col, U16(d, 4));
                    cell.type = XlsValueType::Number;
                    cell.number = F64(d, 6);
                    break;
                }
                case kRk: {
                    const int row = U16(d, 0), col = U16(d, 2);
                    if (!KeepCell(row, col)) break;
                    XlsCell& cell = AddCell(sheet, row, col, U16(d, 4));
                    cell.type = XlsValueType::Number;
                    cell.number = DecodeRk(U32(d, 6));
                    break;
                }
                case kMulRk: {
                    const int row = U16(d, 0);
                    int col = U16(d, 2);
                    for (size_t at = 4; at + 6 <= d.size() - std::min<size_t>(2, d.size()); at += 6, ++col) {
                        if (!KeepCell(row, col)) continue;
                        XlsCell& cell = AddCell(sheet, row, col, U16(d, at));
                        cell.type = XlsValueType::Number;
                        cell.number = DecodeRk(U32(d, at + 2));
                    }
                    break;
                }
                case kBoolErr: {
                    const int row = U16(d, 0), col = U16(d, 2);
                    if (!KeepCell(row, col) || d.size() < 8) break;
                    XlsCell& cell = AddCell(sheet, row, col, U16(d, 4));
                    if (d[7]) {
                        cell.type = XlsValueType::Error;
                        cell.errorCode = d[6];
                    } else {
                        cell.type = XlsValueType::Boolean;
                        cell.boolean = d[6] != 0;
                    }
                    break;
                }
                case kBlank: {
                    const int row = U16(d, 0), col = U16(d, 2);
                    if (KeepCell(row, col)) AddCell(sheet, row, col, U16(d, 4));
                    break;
                }
                case kMulBlank: {
                    const int row = U16(d, 0);
                    int col = U16(d, 2);
                    for (size_t at = 4; at + 2 <= d.size() - std::min<size_t>(2, d.size()); at += 2, ++col) {
                        if (KeepCell(row, col)) AddCell(sheet, row, col, U16(d, at));
                    }
                    break;
                }
                case kFormula: {
                    pendingString = -1;
                    const int row = U16(d, 0), col = U16(d, 2);
                    if (!KeepCell(row, col) || d.size() < 22) break;
                    XlsCell& cell = AddCell(sheet, row, col, U16(d, 4));
                    cell.hasFormula = true;
                    if (U16(d, 12) == 0xFFFF) {
                        switch (d[6]) {
                            case 0:   // text, in the STRING record that follows
                                cell.type = XlsValueType::Text;
                                pendingString = static_cast<int>(sheet.cells.size()) - 1;
                                break;
                            case 1:
                                cell.type = XlsValueType::Boolean;
                                cell.boolean = d[8] != 0;
                                break;
                            case 2:
                                cell.type = XlsValueType::Error;
                                cell.errorCode = d[8];
                                break;
                            default:  // an empty string
                                cell.type = XlsValueType::Text;
                                break;
                        }
                    } else {
                        cell.type = XlsValueType::Number;
                        cell.number = F64(d, 6);
                    }
                    if (opt_.translateFormulas) {
                        const size_t cce = U16(d, 20);
                        if (22 + cce <= d.size()) {
                            std::vector<uint8_t> tokens(d.begin() + 22,
                                                        d.begin() + 22 + static_cast<std::ptrdiff_t>(cce));
                            cell.formula = TranslateCellFormula(tokens, row, col, shared);
                        }
                    }
                    break;
                }
                case kString:
                    if (pendingString >= 0 && pendingString < static_cast<int>(sheet.cells.size())) {
                        Cursor c(r);
                        sheet.cells[static_cast<size_t>(pendingString)].text = ReadString(c, 2);
                    }
                    pendingString = -1;
                    break;
                case kShrFmla:
                case kArray:
                case kTable:
                    break;   // between a FORMULA and its STRING; read above
                default:
                    break;
            }
        }
    }

    std::string TranslateCellFormula(const std::vector<uint8_t>& tokens, int row, int col,
                                     const std::map<std::pair<int, int>, SharedFormula>& shared) {
        std::string text;
        if (!tokens.empty() && tokens[0] == 0x01 && tokens.size() >= 5) {
            // PtgExp: the formula is shared (or an array / table formula,
            // which have no SHRFMLA and stay untranslated).
            auto it = shared.find({U16(tokens, 1), U16(tokens, 3)});
            if (it == shared.end()) return std::string();
            if (!Decompile(it->second.tokens, row, col, true, text)) return std::string();
        } else if (!Decompile(tokens, row, col, false, text)) {
            return std::string();
        }
        return "=" + text;
    }

    // ----- formulas -----

    // The worksheet (BOUNDSHEET index) a 3-D reference's EXTERNSHEET entry
    // names, when it is one sheet of this workbook.
    bool SheetOfExtern(int ixti, int& sheet) const {
        if (ixti < 0 || ixti >= static_cast<int>(externSheets_.size())) return false;
        const ExternSheet& x = externSheets_[static_cast<size_t>(ixti)];
        if (x.supBook < 0 || x.supBook >= static_cast<int>(supBookIsSelf_.size()) ||
            !supBookIsSelf_[static_cast<size_t>(x.supBook)])
            return false;
        if (x.firstSheet != x.lastSheet || x.firstSheet >= static_cast<int>(sheets_.size()))
            return false;
        sheet = x.firstSheet;
        return true;
    }

    enum Precedence { kCompare = 1, kConcat, kAdditive, kMultiplicative, kPower, kUnary,
                      kPercent, kPrimary };

    struct Operand {
        std::string text;
        int precedence = kPrimary;
    };

    // Excel stores a formula as a postfix token list (RPN); this rebuilds the
    // infix text, adding parentheses where precedence needs them (Excel keeps
    // the user's own as tParen tokens, but not every writer does).
    // `relativeForm`: the tokens are a shared formula's, expanded for the
    // cell at baseRow / baseCol (its tRefN / tAreaN are offsets from it).
    bool Decompile(const std::vector<uint8_t>& t, int baseRow, int baseCol, bool relativeForm,
                   std::string& out) const {
        std::vector<Operand> stack;
        size_t pos = 0;
        auto need = [&](size_t n) { return pos + n <= t.size(); };
        auto pop = [&](Operand& o) {
            if (stack.empty()) return false;
            o = std::move(stack.back());
            stack.pop_back();
            return true;
        };
        auto wrap = [](const Operand& o, bool parens) {
            return parens ? "(" + o.text + ")" : o.text;
        };
        auto binary = [&](const char* op, int precedence) {
            Operand right, left;
            if (!pop(right) || !pop(left)) return false;
            Operand result;
            result.text = wrap(left, left.precedence < precedence) + op +
                          wrap(right, right.precedence <= precedence);
            result.precedence = precedence;
            stack.push_back(std::move(result));
            return true;
        };

        while (pos < t.size()) {
            const uint8_t ptg = t[pos++];
            if (ptg < 0x20) {
                switch (ptg) {
                    case 0x03: if (!binary("+", kAdditive)) return false; break;
                    case 0x04: if (!binary("-", kAdditive)) return false; break;
                    case 0x05: if (!binary("*", kMultiplicative)) return false; break;
                    case 0x06: if (!binary("/", kMultiplicative)) return false; break;
                    case 0x07: if (!binary("^", kPower)) return false; break;
                    case 0x08: if (!binary("&", kConcat)) return false; break;
                    case 0x09: if (!binary("<", kCompare)) return false; break;
                    case 0x0A: if (!binary("<=", kCompare)) return false; break;
                    case 0x0B: if (!binary("=", kCompare)) return false; break;
                    case 0x0C: if (!binary(">=", kCompare)) return false; break;
                    case 0x0D: if (!binary(">", kCompare)) return false; break;
                    case 0x0E: if (!binary("<>", kCompare)) return false; break;
                    case 0x12:
                    case 0x13: {
                        Operand o;
                        if (!pop(o)) return false;
                        Operand r;
                        r.text = std::string(ptg == 0x12 ? "+" : "-") + wrap(o, o.precedence < kUnary);
                        r.precedence = kUnary;
                        stack.push_back(std::move(r));
                        break;
                    }
                    case 0x14: {
                        Operand o;
                        if (!pop(o)) return false;
                        Operand r;
                        r.text = wrap(o, o.precedence < kPercent) + "%";
                        r.precedence = kPercent;
                        stack.push_back(std::move(r));
                        break;
                    }
                    case 0x15: {
                        Operand o;
                        if (!pop(o)) return false;
                        o.text = "(" + o.text + ")";
                        o.precedence = kPrimary;
                        stack.push_back(std::move(o));
                        break;
                    }
                    case 0x17: {   // string constant
                        if (!need(1)) return false;
                        std::string s;
                        if (biff_ >= 8) {
                            const size_t cch = t[pos];
                            if (!need(2)) return false;
                            const bool wide = (t[pos + 1] & 0x01) != 0;
                            pos += 2;
                            std::vector<uint16_t> units;
                            for (size_t k = 0; k < cch; ++k) {
                                if (!need(wide ? 2 : 1)) return false;
                                units.push_back(wide ? U16(t, pos) : t[pos]);
                                pos += wide ? 2 : 1;
                            }
                            s = Utf16ToUtf8(units);
                        } else {
                            const size_t cch = t[pos++];
                            if (!need(cch)) return false;
                            s = DecodeCodePage(t.data() + pos, cch, wb_.codePage);
                            pos += cch;
                        }
                        Operand o;
                        o.text = "\"";
                        for (char ch : s) {
                            o.text.push_back(ch);
                            if (ch == '"') o.text.push_back('"');
                        }
                        o.text.push_back('"');
                        stack.push_back(std::move(o));
                        break;
                    }
                    case 0x19: {   // tAttr: control tokens, and SUM of one argument
                        if (!need(3)) return false;
                        const uint8_t grbit = t[pos];
                        const uint16_t data = U16(t, pos + 1);
                        pos += 3;
                        if (grbit & 0x04) {        // tAttrChoose: a jump table follows
                            if (!need((static_cast<size_t>(data) + 1) * 2)) return false;
                            pos += (static_cast<size_t>(data) + 1) * 2;
                        }
                        if (grbit & 0x10) {        // tAttrSum
                            Operand o;
                            if (!pop(o)) return false;
                            o.text = "SUM(" + o.text + ")";
                            o.precedence = kPrimary;
                            stack.push_back(std::move(o));
                        }
                        break;
                    }
                    case 0x1D: {
                        if (!need(1)) return false;
                        Operand o;
                        o.text = t[pos++] ? "TRUE" : "FALSE";
                        stack.push_back(std::move(o));
                        break;
                    }
                    case 0x1E: {
                        if (!need(2)) return false;
                        Operand o;
                        o.text = std::to_string(U16(t, pos));
                        pos += 2;
                        stack.push_back(std::move(o));
                        break;
                    }
                    case 0x1F: {
                        if (!need(8)) return false;
                        const double v = F64(t, pos);
                        pos += 8;
                        if (!std::isfinite(v)) return false;
                        Operand o;
                        o.text = NumberLiteral(std::fabs(v));
                        // A negative constant reads as a unary minus.
                        if (v < 0) {
                            o.text = "-" + o.text;
                            o.precedence = kUnary;
                        }
                        stack.push_back(std::move(o));
                        break;
                    }
                    default:
                        // tExp/tTbl inside a formula, union, intersection,
                        // range operator, missing argument, error constant,
                        // the extended tokens: not expressible - keep the
                        // cached value.
                        return false;
                }
                continue;
            }

            const uint8_t base = static_cast<uint8_t>((ptg & 0x1F) | 0x20);
            switch (base) {
                case 0x21:     // tFunc
                case 0x22: {   // tFuncVar
                    size_t argc = 0;
                    uint16_t index = 0;
                    if (base == 0x22) {
                        if (!need(3)) return false;
                        argc = t[pos] & 0x7F;
                        index = U16(t, pos + 1) & 0x7FFF;
                        pos += 3;
                    } else {
                        if (!need(2)) return false;
                        index = U16(t, pos);
                        pos += 2;
                    }
                    const FunctionInfo* fn = FindFunction(index);
                    if (!fn) return false;   // an add-in, a macro function, or unknown
                    if (base == 0x21) {
                        if (fn->minArgs != fn->maxArgs) return false;
                        argc = fn->minArgs;
                    }
                    if (argc > stack.size()) return false;
                    std::string text = std::string(fn->name) + "(";
                    const size_t first = stack.size() - argc;
                    for (size_t k = first; k < stack.size(); ++k) {
                        if (k > first) text += ",";
                        text += stack[k].text;
                    }
                    text += ")";
                    stack.resize(first);
                    Operand o;
                    o.text = std::move(text);
                    stack.push_back(std::move(o));
                    break;
                }
                case 0x23: {   // tName
                    const size_t size = biff_ >= 8 ? 4 : 14;
                    if (!need(size)) return false;
                    const int nameIndex = static_cast<int>(U16(t, pos)) - 1;
                    pos += size;
                    if (nameIndex < 0 || nameIndex >= static_cast<int>(nameModelIndex_.size()))
                        return false;
                    const int model = nameModelIndex_[static_cast<size_t>(nameIndex)];
                    if (model < 0) return false;
                    const XlsDefinedName& n = wb_.names[static_cast<size_t>(model)];
                    if (n.builtIn || n.refSheet < 0) return false;
                    Operand o;
                    o.text = n.name;
                    stack.push_back(std::move(o));
                    break;
                }
                case 0x24:     // tRef
                case 0x2C: {   // tRefN
                    int row, col;
                    bool rowRel, colRel;
                    if (!ReadRef(t, pos, base == 0x2C, baseRow, baseCol, row, col, rowRel, colRel))
                        return false;
                    Operand o;
                    o.text = CellName(row, col, rowRel, colRel);
                    stack.push_back(std::move(o));
                    break;
                }
                case 0x25:     // tArea
                case 0x2D: {   // tAreaN
                    std::string text;
                    bool anyRelative = false;
                    if (!ReadArea(t, pos, base == 0x2D, baseRow, baseCol, text, anyRelative))
                        return false;
                    Operand o;
                    o.text = std::move(text);
                    stack.push_back(std::move(o));
                    break;
                }
                case 0x26:     // tMemArea / tMemErr / tMemNoMem: a sub-expression
                case 0x27:     // follows that computes the reference itself
                case 0x28:
                    if (!need(6)) return false;
                    pos += 6;
                    break;
                case 0x29:     // tMemFunc
                    if (!need(2)) return false;
                    pos += 2;
                    break;
                case 0x3A:     // tRef3d
                case 0x3B: {   // tArea3d
                    if (biff_ < 8 || !need(2)) return false;
                    int sheet = -1;
                    if (!SheetOfExtern(U16(t, pos), sheet)) return false;
                    pos += 2;
                    std::string prefix;
                    if (!QuotedSheet(sheets_[static_cast<size_t>(sheet)].name, prefix)) return false;
                    // In a shared formula a relative 3-D reference is
                    // stored relative to the cell using it, and which form
                    // writers use is not settled: keep the cached value.
                    std::string text;
                    bool anyRelative = false;
                    if (base == 0x3A) {
                        int row, col;
                        bool rowRel, colRel;
                        if (!ReadRef(t, pos, false, baseRow, baseCol, row, col, rowRel, colRel))
                            return false;
                        anyRelative = rowRel || colRel;
                        text = CellName(row, col, rowRel, colRel);
                    } else if (!ReadArea(t, pos, false, baseRow, baseCol, text, anyRelative)) {
                        return false;
                    }
                    if (relativeForm && anyRelative) return false;
                    Operand o;
                    o.text = prefix + text;
                    stack.push_back(std::move(o));
                    break;
                }
                default:
                    // tArray (constants), tRefErr / tAreaErr (#REF!), tNameX
                    // (another workbook's name or an add-in), the 3-D error
                    // forms: not expressible.
                    return false;
            }
        }
        if (stack.size() != 1) return false;
        out = std::move(stack.back().text);
        return true;
    }

    // A cell reference token. BIFF8 keeps the relative flags in the column
    // word, BIFF5 in the row word. In the relative form (shared formulas,
    // tRefN) a relative row or column is an offset from the cell using it.
    bool ReadRef(const std::vector<uint8_t>& t, size_t& pos, bool relativeForm, int baseRow,
                 int baseCol, int& row, int& col, bool& rowRel, bool& colRel) const {
        if (biff_ >= 8) {
            if (pos + 4 > t.size()) return false;
            const uint16_t rw = U16(t, pos), cw = U16(t, pos + 2);
            pos += 4;
            rowRel = (cw & 0x8000) != 0;
            colRel = (cw & 0x4000) != 0;
            row = rw;
            col = cw & 0x00FF;
            if (relativeForm) {
                if (rowRel) row = (baseRow + static_cast<int16_t>(rw)) & 0xFFFF;
                if (colRel) col = (baseCol + static_cast<int8_t>(cw & 0xFF)) & 0xFF;
            }
        } else {
            if (pos + 3 > t.size()) return false;
            const uint16_t rw = U16(t, pos);
            const uint8_t cb = t[pos + 2];
            pos += 3;
            rowRel = (rw & 0x8000) != 0;
            colRel = (rw & 0x4000) != 0;
            row = rw & 0x3FFF;
            col = cb;
            if (relativeForm) {
                if (rowRel) {
                    int offset = rw & 0x3FFF;
                    if (offset & 0x2000) offset -= 0x4000;   // 14-bit two's complement
                    row = (baseRow + offset) & 0x3FFF;
                }
                if (colRel) col = (baseCol + static_cast<int8_t>(cb)) & 0xFF;
            }
        }
        return true;
    }

    bool ReadArea(const std::vector<uint8_t>& t, size_t& pos, bool relativeForm, int baseRow,
                  int baseCol, std::string& out, bool& anyRelative) const {
        int r1, c1, r2, c2;
        bool r1Rel, c1Rel, r2Rel, c2Rel;
        if (biff_ >= 8) {
            if (pos + 8 > t.size()) return false;
            // rwFirst, rwLast, colFirst, colLast: regroup as two references.
            std::vector<uint8_t> first = {t[pos], t[pos + 1], t[pos + 4], t[pos + 5]};
            std::vector<uint8_t> last = {t[pos + 2], t[pos + 3], t[pos + 6], t[pos + 7]};
            pos += 8;
            size_t p = 0;
            if (!ReadRef(first, p, relativeForm, baseRow, baseCol, r1, c1, r1Rel, c1Rel)) return false;
            p = 0;
            if (!ReadRef(last, p, relativeForm, baseRow, baseCol, r2, c2, r2Rel, c2Rel)) return false;
        } else {
            if (pos + 6 > t.size()) return false;
            std::vector<uint8_t> first = {t[pos], t[pos + 1], t[pos + 4]};
            std::vector<uint8_t> last = {t[pos + 2], t[pos + 3], t[pos + 5]};
            pos += 6;
            size_t p = 0;
            if (!ReadRef(first, p, relativeForm, baseRow, baseCol, r1, c1, r1Rel, c1Rel)) return false;
            p = 0;
            if (!ReadRef(last, p, relativeForm, baseRow, baseCol, r2, c2, r2Rel, c2Rel)) return false;
        }
        anyRelative = r1Rel || c1Rel || r2Rel || c2Rel;
        out = CellName(r1, c1, r1Rel, c1Rel) + ":" + CellName(r2, c2, r2Rel, c2Rel);
        return true;
    }
};

// ===== HTML TABLES AND EXCEL 2003 XML: SHARED HELPERS =====

bool ReadWholeFile(const std::string& path, std::string& out) {
    std::FILE* f = OpenFileUtf8(path, "rb");
    if (!f) return false;
    char buffer[64 * 1024];
    size_t got = 0;
    out.clear();
    while ((got = std::fread(buffer, 1, sizeof(buffer), f)) > 0) out.append(buffer, got);
    std::fclose(f);
    return true;
}

std::string Trimmed(const std::string& text) {
    size_t a = 0, b = text.size();
    while (a < b && std::isspace(static_cast<unsigned char>(text[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(text[b - 1]))) --b;
    return text.substr(a, b - a);
}

// The whole text is one dot-decimal number (a leading '+' allowed).
bool WholeNumber(const std::string& text, double& out) {
    const std::string t = Trimmed(text);
    if (t.empty()) return false;
    const char* first = t.c_str();
    const char* last = first + t.size();
    if (*first == '+') ++first;
    double value = 0.0;
    const char* end = ParseFloatClassic(first, last, value);
    if (end != last || end == first || !std::isfinite(value)) return false;
    out = value;
    return true;
}

uint8_t ErrorCodeFromText(const std::string& text) {
    static const std::pair<const char*, uint8_t> codes[] = {
        {"#NULL!", 0x00}, {"#DIV/0!", 0x07}, {"#VALUE!", 0x0F}, {"#REF!", 0x17},
        {"#NAME?", 0x1D}, {"#NUM!", 0x24}, {"#N/A", 0x2A}};
    for (const auto& c : codes) {
        if (text == c.first) return c.second;
    }
    return 0x0F;
}

void KeepWithin(XlsSheet& sheet, const XlsReadOptions& opt, XlsCell cell) {
    if ((opt.maxRows >= 0 && cell.row >= opt.maxRows) ||
        (opt.maxColumns >= 0 && cell.col >= opt.maxColumns))
        return;
    sheet.cells.push_back(std::move(cell));
}

// ===== HTML TABLES =====

// A page of tables is one sheet, the tables one under another, the way Excel
// opens a web page saved as .xls. Header cells (<th>) are bold; a cell's
// number is Excel's own x:num attribute when it wrote one, else its text
// when that is a plain dot-decimal number.
bool ReadHtmlWorkbook(const std::string& html, XlsWorkbook& out, const XlsReadOptions& opt,
                      std::string& error) {
    HTML::Parser parser;
    HTML::Document doc = parser.Parse(html);
    out.biffVersion = 0;
    XlsFont bold;
    bold.bold = true;
    out.fonts = {XlsFont(), bold};
    XlsCellFormat plain, header;
    plain.numberFormatCode = header.numberFormatCode = "General";
    header.font = 1;
    out.formats = {plain, header};

    std::vector<HTML::Node*> rows;
    if (doc.root) {
        doc.root->ForEachElement([&](HTML::Node& node) {
            if (!node.IsElement("tr")) return true;
            // Rows of a table nested in a cell belong to that cell's text.
            int tables = 0;
            for (HTML::Node* up = node.parent; up; up = up->parent) {
                if (up->IsElement("table")) ++tables;
            }
            if (tables <= 1) rows.push_back(&node);
            return true;
        });
    }
    if (rows.empty()) {
        error = "The page holds no table to read as a sheet";
        return false;
    }
    XlsSheet sheet;
    sheet.name = "Sheet1";
    std::set<std::pair<int, int>> covered;   // cells spanned by an earlier cell
    int row = 0;
    for (HTML::Node* tr : rows) {
        if (opt.maxRows >= 0 && row >= opt.maxRows) break;
        int col = 0;
        for (const HTML::NodePtr& child : tr->children) {
            const bool isHeader = child->IsElement("th");
            if (!isHeader && !child->IsElement("td")) continue;
            while (covered.count({row, col})) ++col;
            auto span = [&](const char* name) {
                const long v = std::strtol(child->GetAttribute(name).c_str(), nullptr, 10);
                return static_cast<int>(std::clamp(v, 1L, 1000L));
            };
            const int colspan = span("colspan");
            const int rowspan = span("rowspan");
            XlsCell cell;
            cell.row = row;
            cell.col = col;
            cell.format = isHeader ? 1 : 0;
            const std::string text = Trimmed(HTML::ExtractPlainText(*child));
            const std::string excelNumber = child->GetAttribute("x:num");
            double number = 0.0;
            if (child->HasAttribute("x:str")) {
                cell.type = XlsValueType::Text;
                cell.text = text;
            } else if (WholeNumber(excelNumber.empty() ? text : excelNumber, number)) {
                cell.type = XlsValueType::Number;
                cell.number = number;
            } else if (!text.empty()) {
                cell.type = XlsValueType::Text;
                cell.text = text;
            }
            if (cell.type != XlsValueType::Empty) KeepWithin(sheet, opt, std::move(cell));
            if (colspan > 1 || rowspan > 1) {
                sheet.merges.push_back({row, col, row + rowspan - 1, col + colspan - 1});
                for (int r = row; r < row + rowspan; ++r) {
                    for (int c = col; c < col + colspan; ++c) {
                        if (r != row || c != col) covered.insert({r, c});
                    }
                }
            }
            col += colspan;
        }
        ++row;
    }
    out.sheets.push_back(std::move(sheet));
    return true;
}

// ===== EXCEL 2003 XML SPREADSHEET =====

const char* LocalName(const char* name) {
    const char* colon = std::strrchr(name, ':');
    return colon ? colon + 1 : name;
}

bool IsNamed(const tinyxml2::XMLElement* e, const char* local) {
    return std::strcmp(LocalName(e->Name()), local) == 0;
}

// An attribute by its local name: SpreadsheetML writes ss:Name, but a writer
// may bind the namespace to another prefix or to none.
const char* XAttr(const tinyxml2::XMLElement* e, const char* local) {
    for (const tinyxml2::XMLAttribute* a = e->FirstAttribute(); a; a = a->Next()) {
        if (std::strncmp(a->Name(), "xmlns", 5) == 0) continue;
        if (std::strcmp(LocalName(a->Name()), local) == 0) return a->Value();
    }
    return nullptr;
}

const tinyxml2::XMLElement* XChild(const tinyxml2::XMLElement* e, const char* local) {
    for (const tinyxml2::XMLElement* c = e->FirstChildElement(); c; c = c->NextSiblingElement()) {
        if (IsNamed(c, local)) return c;
    }
    return nullptr;
}

long XInt(const tinyxml2::XMLElement* e, const char* local, long fallback) {
    const char* v = XAttr(e, local);
    return v && *v ? std::strtol(v, nullptr, 10) : fallback;
}

double XDouble(const tinyxml2::XMLElement* e, const char* local, double fallback) {
    const char* v = XAttr(e, local);
    double value = fallback;
    if (v && *v) TryParseFloat(v, value);
    return value;
}

bool XBool(const tinyxml2::XMLElement* e, const char* local) {
    const char* v = XAttr(e, local);
    return v && (std::strcmp(v, "1") == 0 || std::strcmp(v, "true") == 0);
}

bool HexColor(const char* text, Color& out) {
    if (!text || text[0] != '#' || std::strlen(text) != 7) return false;
    const unsigned long rgb = std::strtoul(text + 1, nullptr, 16);
    out = RgbColor(static_cast<uint32_t>(rgb));
    return true;
}

void CollectText(const tinyxml2::XMLNode* node, std::string& out) {
    for (const tinyxml2::XMLNode* c = node->FirstChild(); c; c = c->NextSibling()) {
        if (const tinyxml2::XMLText* t = c->ToText()) out += t->Value();
        else CollectText(c, out);
    }
}

// Days since 1899-12-30, Excel's 1900-system serial.
double SerialFromIsoDateTime(const std::string& text, bool& ok) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0;
    double sec = 0.0;
    ok = std::sscanf(text.c_str(), "%d-%d-%d", &y, &mo, &d) == 3 && mo >= 1 && mo <= 12 &&
         d >= 1 && d <= 31;
    if (!ok) return 0.0;
    const size_t t = text.find('T');
    if (t != std::string::npos) {
        std::sscanf(text.c_str() + t + 1, "%d:%d", &h, &mi);
        const size_t secAt = text.find(':', text.find(':', t) + 1);
        if (secAt != std::string::npos) TryParseFloat(text.substr(secAt + 1), sec);
    }
    // Howard Hinnant's days_from_civil.
    auto days = [](int yy, unsigned mm, unsigned dd) {
        yy -= mm <= 2;
        const int era = (yy >= 0 ? yy : yy - 399) / 400;
        const unsigned yoe = static_cast<unsigned>(yy - era * 400);
        const unsigned doy = (153 * (mm + (mm > 2 ? -3 : 9)) + 2) / 5 + dd - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + static_cast<int>(doe) - 719468;
    };
    const double serial = days(y, static_cast<unsigned>(mo), static_cast<unsigned>(d)) -
                          days(1899, 12, 30);
    return serial + (h * 3600.0 + mi * 60.0 + sec) / 86400.0;
}

// R1C1 (absolute "R2C3", relative "R[-1]C[2]", "RC" = this cell) at `pos`.
bool ParseR1C1(const std::string& f, size_t& pos, int baseRow, int baseCol, int& row, int& col,
               bool& rowRel, bool& colRel) {
    size_t p = pos;
    auto part = [&](char letter, int base, int& value, bool& relative) {
        if (p >= f.size() || std::toupper(static_cast<unsigned char>(f[p])) != letter) return false;
        ++p;
        if (p < f.size() && f[p] == '[') {
            const size_t close = f.find(']', p);
            if (close == std::string::npos) return false;
            char* end = nullptr;
            const std::string inner = f.substr(p + 1, close - p - 1);
            const long v = std::strtol(inner.c_str(), &end, 10);
            if (inner.empty() || *end) return false;
            value = base + static_cast<int>(v);
            relative = true;
            p = close + 1;
        } else if (p < f.size() && std::isdigit(static_cast<unsigned char>(f[p]))) {
            long v = 0;
            while (p < f.size() && std::isdigit(static_cast<unsigned char>(f[p])) && v < 10000000) {
                v = v * 10 + (f[p++] - '0');
            }
            value = static_cast<int>(v) - 1;
            relative = false;
        } else {
            value = base;
            relative = true;
        }
        return true;
    };
    if (!part('R', baseRow, row, rowRel) || !part('C', baseCol, col, colRel)) return false;
    if (p < f.size() && (std::isalnum(static_cast<unsigned char>(f[p])) || f[p] == '_' ||
                         f[p] == '(' || f[p] == '.'))
        return false;   // part of a longer name ("RC4(")
    if (row < 0 || col < 0 || row >= 1048576 || col >= 16384) return false;
    pos = p;
    return true;
}

// SpreadsheetML keeps formulas in R1C1 notation, relative to their cell, and
// Excel's Sheet!A1 sheet syntax; the engine reads A1 and 'Sheet'.A1. False for
// what the engine cannot read: an error literal or array constant, an
// external workbook, a whole row or column.
bool R1C1ToNative(const std::string& f, int baseRow, int baseCol, std::string& out) {
    out.clear();
    auto isWord = [](char c) {
        const unsigned char u = static_cast<unsigned char>(c);
        return std::isalnum(u) || c == '_' || c == '.' || u >= 0x80;
    };
    size_t i = 0;
    while (i < f.size()) {
        const char c = f[i];
        if (c == '"') {
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
            continue;
        }
        if (c == '#' || c == '{' || c == '[') return false;
        const bool wordStart = (std::isalpha(static_cast<unsigned char>(c)) || c == '_' ||
                                static_cast<unsigned char>(c) >= 0x80) &&
                               (i == 0 || !isWord(f[i - 1]));
        if (c != '\'' && !wordStart) {
            out.push_back(c);
            ++i;
            continue;
        }
        // A sheet prefix ('Quoted Name'! or Name!), a reference, or a word.
        std::string sheet;
        size_t refAt = i;
        size_t wordEnd = i;
        if (c == '\'') {
            size_t j = i + 1;
            while (j < f.size()) {
                if (f[j] == '\'') {
                    if (j + 1 < f.size() && f[j + 1] == '\'') { sheet.push_back('\''); j += 2; continue; }
                    break;
                }
                sheet.push_back(f[j++]);
            }
            if (j + 1 >= f.size() || f[j + 1] != '!') return false;
            refAt = j + 2;
        } else {
            while (wordEnd < f.size() && isWord(f[wordEnd])) ++wordEnd;
            if (wordEnd < f.size() && f[wordEnd] == '!') {
                sheet = f.substr(i, wordEnd - i);
                refAt = wordEnd + 1;
            }
        }
        size_t p = refAt;
        int r1, c1;
        bool rr1, cr1;
        if (ParseR1C1(f, p, baseRow, baseCol, r1, c1, rr1, cr1)) {
            std::string text = CellName(r1, c1, rr1, cr1);
            size_t q = p + 1;
            int r2, c2;
            bool rr2, cr2;
            if (p < f.size() && f[p] == ':' && ParseR1C1(f, q, baseRow, baseCol, r2, c2, rr2, cr2)) {
                text += ":" + CellName(r2, c2, rr2, cr2);
                p = q;
            }
            if (!sheet.empty()) {
                std::string prefix;
                if (!QuotedSheet(sheet, prefix)) return false;
                text = prefix + text;
            }
            out += text;
            i = p;
            continue;
        }
        if (!sheet.empty() || c == '\'') return false;
        out.append(f, i, wordEnd - i);
        i = wordEnd;
    }
    return true;
}

struct XmlFormats {
    std::map<std::string, int> byId;   // ss:ID -> index into XlsWorkbook::formats
    int nextCustomId = 164;
};

// SpreadsheetML's named number formats, as Excel's built-in ids.
void ApplyXmlNumberFormat(const std::string& name, XlsCellFormat& f, XmlFormats& state) {
    static const std::pair<const char*, int> named[] = {
        {"General", 0}, {"General Number", 0}, {"General Date", 22}, {"Medium Date", 15},
        {"Short Date", 14}, {"Long Time", 19}, {"Medium Time", 18}, {"Short Time", 20},
        {"Currency", 7}, {"Fixed", 2}, {"Standard", 4}, {"Percent", 10}, {"Scientific", 11},
        {"Yes/No", 0}, {"True/False", 0}, {"On/Off", 0}};
    for (const auto& n : named) {
        if (name == n.first) {
            f.numberFormatId = n.second;
            f.numberFormatCode = BuiltInFormatCode(n.second);
            return;
        }
    }
    if (name == "Long Date") {
        f.numberFormatId = state.nextCustomId++;
        f.numberFormatCode = "dddd, mmmm dd, yyyy";
        return;
    }
    f.numberFormatId = state.nextCustomId++;
    f.numberFormatCode = name;
}

int XmlBorderStyle(const char* lineStyle, long weight) {
    const std::string s = lineStyle ? lineStyle : "";
    if (s.empty() || s == "None") return 0;
    if (s == "Double") return 6;
    if (s == "Dot") return 4;
    if (s == "Dash") return weight >= 2 ? 8 : 3;
    if (s == "DashDot") return weight >= 2 ? 10 : 9;
    if (s == "DashDotDot") return weight >= 2 ? 12 : 11;
    if (s == "SlantDashDot") return 13;
    return weight >= 3 ? 5 : weight == 2 ? 2 : weight == 0 ? 7 : 1;   // Continuous
}

void ReadXmlStyles(const tinyxml2::XMLElement* styles, XlsWorkbook& out, XmlFormats& state) {
    for (const tinyxml2::XMLElement* st = styles->FirstChildElement(); st;
         st = st->NextSiblingElement()) {
        if (!IsNamed(st, "Style")) continue;
        const char* id = XAttr(st, "ID");
        if (!id) continue;
        XlsCellFormat f;
        f.numberFormatCode = "General";
        XlsFont font;
        if (const char* parent = XAttr(st, "Parent")) {
            auto it = state.byId.find(parent);
            if (it != state.byId.end()) {
                f = out.formats[static_cast<size_t>(it->second)];
                font = out.fonts[static_cast<size_t>(f.font)];
            }
        }
        if (const tinyxml2::XMLElement* e = XChild(st, "Font")) {
            if (const char* v = XAttr(e, "FontName")) font.name = v;
            font.sizePoints = XDouble(e, "Size", font.sizePoints);
            if (XAttr(e, "Bold")) font.bold = XBool(e, "Bold");
            if (XAttr(e, "Italic")) font.italic = XBool(e, "Italic");
            if (XAttr(e, "StrikeThrough")) font.strikethrough = XBool(e, "StrikeThrough");
            if (const char* u = XAttr(e, "Underline")) {
                const std::string us = u;
                font.underline = us == "Double" || us == "DoubleAccounting" ? 2 : us == "None" ? 0 : 1;
            }
            if (const char* v = XAttr(e, "VerticalAlign")) {
                font.superscript = std::strcmp(v, "Superscript") == 0;
                font.subscript = std::strcmp(v, "Subscript") == 0;
            }
            Color color;
            if (HexColor(XAttr(e, "Color"), color)) {
                font.color = color;
                font.automaticColor = false;
            }
        }
        if (const tinyxml2::XMLElement* e = XChild(st, "Interior")) {
            Color color;
            const char* pattern = XAttr(e, "Pattern");
            if (HexColor(XAttr(e, "Color"), color) && (!pattern || std::strcmp(pattern, "None") != 0)) {
                f.fillPattern = 1;
                f.patternColor = color;
            }
        }
        if (const tinyxml2::XMLElement* e = XChild(st, "Alignment")) {
            if (const char* h = XAttr(e, "Horizontal")) {
                const std::string hs = h;
                f.horizontalAlign = hs == "Left" ? 1 : hs == "Center" ? 2 : hs == "Right" ? 3
                                  : hs == "Fill" ? 4 : hs == "Justify" ? 5
                                  : hs == "CenterAcrossSelection" ? 6 : hs == "Distributed" ? 7 : 0;
            }
            if (const char* v = XAttr(e, "Vertical")) {
                const std::string vs = v;
                f.verticalAlign = vs == "Top" ? 0 : vs == "Center" ? 1 : vs == "Justify" ? 3
                                : vs == "Distributed" ? 4 : 2;
            }
            if (XAttr(e, "WrapText")) f.wrapText = XBool(e, "WrapText");
            if (XAttr(e, "ShrinkToFit")) f.shrinkToFit = XBool(e, "ShrinkToFit");
            f.rotation = static_cast<int>(std::clamp(XInt(e, "Rotate", f.rotation), -90L, 90L));
            f.indent = static_cast<int>(XInt(e, "Indent", f.indent));
        }
        if (const tinyxml2::XMLElement* borders = XChild(st, "Borders")) {
            for (const tinyxml2::XMLElement* b = borders->FirstChildElement(); b;
                 b = b->NextSiblingElement()) {
                const char* position = XAttr(b, "Position");
                if (!position) continue;
                XlsBorderLine line;
                line.style = XmlBorderStyle(XAttr(b, "LineStyle"), XInt(b, "Weight", 1));
                HexColor(XAttr(b, "Color"), line.color);
                const std::string ps = position;
                if (ps == "Left") f.left = line;
                else if (ps == "Right") f.right = line;
                else if (ps == "Top") f.top = line;
                else if (ps == "Bottom") f.bottom = line;
            }
        }
        if (const tinyxml2::XMLElement* e = XChild(st, "NumberFormat")) {
            if (const char* v = XAttr(e, "Format")) ApplyXmlNumberFormat(v, f, state);
        }
        if (const tinyxml2::XMLElement* e = XChild(st, "Protection")) {
            if (XAttr(e, "Protected")) f.locked = XBool(e, "Protected");
            if (XAttr(e, "HideFormula")) f.formulaHidden = XBool(e, "HideFormula");
        }
        f.font = static_cast<int>(out.fonts.size());
        out.fonts.push_back(font);
        state.byId[id] = static_cast<int>(out.formats.size());
        out.formats.push_back(f);
    }
}

// "=Data!R2C2" or "='My Sheet'!R2C2:R4C3" - a defined name's target.
void ReadXmlNameTarget(const std::string& refersTo, const XlsWorkbook& out, XlsDefinedName& n) {
    std::string native;
    const std::string body = refersTo.empty() || refersTo[0] != '=' ? refersTo : refersTo.substr(1);
    if (R1C1ToNative(body, 0, 0, native)) n.formula = native;
    const size_t bang = body.rfind('!');
    if (bang == std::string::npos) return;
    std::string sheet = body.substr(0, bang);
    if (sheet.size() >= 2 && sheet.front() == '\'' && sheet.back() == '\'') {
        sheet = sheet.substr(1, sheet.size() - 2);
    }
    for (size_t i = 0; i < out.sheets.size(); ++i) {
        if (out.sheets[i].name != sheet) continue;
        size_t p = bang + 1;
        int r1, c1, r2, c2;
        bool a, b;
        if (!ParseR1C1(body, p, 0, 0, r1, c1, a, b) || a || b) return;
        r2 = r1;
        c2 = c1;
        if (p < body.size() && body[p] == ':') {
            ++p;
            if (!ParseR1C1(body, p, 0, 0, r2, c2, a, b) || a || b) return;
        }
        if (p != body.size()) return;
        n.refSheet = static_cast<int>(i);
        n.firstRow = r1;
        n.firstColumn = c1;
        n.lastRow = r2;
        n.lastColumn = c2;
        return;
    }
}

bool ReadXmlSpreadsheet(const std::string& xml, XlsWorkbook& out, const XlsReadOptions& opt,
                        std::string& error) {
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS) {
        error = "The Excel 2003 XML workbook is not well-formed XML";
        return false;
    }
    const tinyxml2::XMLElement* root = doc.RootElement();
    if (!root || !IsNamed(root, "Workbook")) {
        error = "The XML file is not an Excel 2003 XML workbook";
        return false;
    }
    out.biffVersion = 0;
    XmlFormats formats;
    out.fonts.push_back(XlsFont());   // index 0: the default, as in a binary workbook
    {
        XlsCellFormat def;
        def.numberFormatCode = "General";
        out.formats.push_back(def);
        formats.byId["Default"] = 0;
    }
    if (const tinyxml2::XMLElement* styles = XChild(root, "Styles")) ReadXmlStyles(styles, out, formats);
    if (formats.byId.count("Default") && formats.byId["Default"] != 0) {
        // The workbook's own Default style sets the font a cell gets by default.
        out.fonts[0] = out.fonts[static_cast<size_t>(out.formats[static_cast<size_t>(
                                                        formats.byId["Default"])].font)];
    }
    auto formatOf = [&](const char* styleId, int fallback) {
        if (!styleId) return fallback;
        auto it = formats.byId.find(styleId);
        return it != formats.byId.end() ? it->second : fallback;
    };

    struct PendingName { std::string name, refersTo; int scope; };
    std::vector<PendingName> pendingNames;
    auto collectNames = [&](const tinyxml2::XMLElement* names, int scope) {
        if (!names) return;
        for (const tinyxml2::XMLElement* nr = names->FirstChildElement(); nr;
             nr = nr->NextSiblingElement()) {
            const char* name = XAttr(nr, "Name");
            const char* refersTo = XAttr(nr, "RefersTo");
            if (name && refersTo) pendingNames.push_back({name, refersTo, scope});
        }
    };
    collectNames(XChild(root, "Names"), -1);

    for (const tinyxml2::XMLElement* ws = root->FirstChildElement(); ws; ws = ws->NextSiblingElement()) {
        if (!IsNamed(ws, "Worksheet")) continue;
        if (opt.maxSheets >= 0 && static_cast<int>(out.sheets.size()) >= opt.maxSheets) break;
        XlsSheet sheet;
        const char* name = XAttr(ws, "Name");
        sheet.name = name ? name : "Sheet" + std::to_string(out.sheets.size() + 1);
        if (const tinyxml2::XMLElement* options = XChild(ws, "WorksheetOptions")) {
            if (const tinyxml2::XMLElement* visible = XChild(options, "Visible")) {
                const char* v = visible->GetText();
                sheet.hidden = v && (std::strcmp(v, "SheetHidden") == 0 ||
                                     std::strcmp(v, "SheetVeryHidden") == 0);
            }
        }
        collectNames(XChild(ws, "Names"), static_cast<int>(out.sheets.size()));
        const tinyxml2::XMLElement* table = XChild(ws, "Table");
        if (table) {
            int col = 0;
            for (const tinyxml2::XMLElement* e = table->FirstChildElement(); e; e = e->NextSiblingElement()) {
                if (!IsNamed(e, "Column")) continue;
                col = static_cast<int>(XInt(e, "Index", col + 1)) - 1;
                const int span = static_cast<int>(std::clamp(XInt(e, "Span", 0), 0L, 16383L));
                XlsColumnInfo info;
                info.firstColumn = col;
                info.lastColumn = col + span;
                const double widthPoints = XDouble(e, "Width", 0.0);
                info.widthChars = widthPoints * 1.33 / 7.0;
                info.customWidth = widthPoints > 0 && !XBool(e, "AutoFitWidth");
                info.hidden = XBool(e, "Hidden");
                if (widthPoints > 0 || info.hidden) sheet.columns.push_back(info);
                col += span;
            }
            int row = -1;
            for (const tinyxml2::XMLElement* r = table->FirstChildElement(); r; r = r->NextSiblingElement()) {
                if (!IsNamed(r, "Row")) continue;
                row = static_cast<int>(XInt(r, "Index", row + 2)) - 1;
                if (opt.maxRows >= 0 && row >= opt.maxRows) break;
                const int rowSpan = static_cast<int>(std::clamp(XInt(r, "Span", 0), 0L, 65535L));
                const double height = XDouble(r, "Height", 0.0);
                const char* autoFit = XAttr(r, "AutoFitHeight");
                const bool customHeight = height > 0 && autoFit &&
                                          (std::strcmp(autoFit, "0") == 0 ||
                                           std::strcmp(autoFit, "false") == 0);
                const bool hidden = XBool(r, "Hidden");
                for (int k = 0; k <= rowSpan && (customHeight || hidden); ++k) {
                    sheet.rows.push_back({row + k, height, customHeight, hidden});
                }
                const int rowFormat = formatOf(XAttr(r, "StyleID"), 0);
                int c = -1;
                for (const tinyxml2::XMLElement* ce = r->FirstChildElement(); ce;
                     ce = ce->NextSiblingElement()) {
                    if (!IsNamed(ce, "Cell")) continue;
                    c = static_cast<int>(XInt(ce, "Index", c + 2)) - 1;
                    XlsCell cell;
                    cell.row = row;
                    cell.col = c;
                    cell.format = formatOf(XAttr(ce, "StyleID"), rowFormat);
                    if (const tinyxml2::XMLElement* data = XChild(ce, "Data")) {
                        std::string text;
                        CollectText(data, text);
                        const std::string type = XAttr(data, "Type") ? XAttr(data, "Type") : "String";
                        bool ok = false;
                        if (type == "Number" && WholeNumber(text, cell.number)) {
                            cell.type = XlsValueType::Number;
                        } else if (type == "DateTime") {
                            cell.number = SerialFromIsoDateTime(text, ok);
                            cell.type = ok ? XlsValueType::Number : XlsValueType::Text;
                            if (!ok) cell.text = text;
                        } else if (type == "Boolean") {
                            cell.type = XlsValueType::Boolean;
                            cell.boolean = Trimmed(text) == "1" || Trimmed(text) == "TRUE";
                        } else if (type == "Error") {
                            cell.type = XlsValueType::Error;
                            cell.errorCode = ErrorCodeFromText(Trimmed(text));
                        } else {
                            cell.type = XlsValueType::Text;
                            cell.text = text;
                        }
                    }
                    if (const char* formula = XAttr(ce, "Formula")) {
                        cell.hasFormula = true;
                        std::string native;
                        if (opt.translateFormulas && !XAttr(ce, "ArrayRange") &&
                            R1C1ToNative(formula, row, c, native) && !native.empty()) {
                            cell.formula = native[0] == '=' ? native : "=" + native;
                        }
                    }
                    const int across = static_cast<int>(std::clamp(XInt(ce, "MergeAcross", 0), 0L, 16383L));
                    const int down = static_cast<int>(std::clamp(XInt(ce, "MergeDown", 0), 0L, 65535L));
                    if ((across > 0 || down > 0) &&
                        (opt.maxRows < 0 || row < opt.maxRows) &&
                        (opt.maxColumns < 0 || c < opt.maxColumns)) {
                        sheet.merges.push_back({row, c, row + down, c + across});
                    }
                    KeepWithin(sheet, opt, std::move(cell));
                    c += across;
                }
                row += rowSpan;
            }
        }
        out.sheets.push_back(std::move(sheet));
    }
    if (out.sheets.empty()) {
        error = "The Excel 2003 XML workbook has no worksheet";
        return false;
    }
    for (const PendingName& p : pendingNames) {
        XlsDefinedName n;
        n.name = p.name;
        n.sheetScope = p.scope;
        n.builtIn = p.name == "Print_Area" || p.name == "Print_Titles" ||
                    p.name == "_FilterDatabase";
        ReadXmlNameTarget(p.refersTo, out, n);
        out.names.push_back(std::move(n));
    }
    return true;
}

bool ReadBiff(UCCompoundFileReader& cfb, XlsWorkbook& out, std::string& error,
              const XlsReadOptions& options) {
    std::vector<uint8_t> stream;
    int streamBiff = 8;
    if (!cfb.ReadStream("Workbook", stream)) {
        if (cfb.ReadStream("Book", stream)) {
            streamBiff = 5;
        } else if (cfb.HasStream("EncryptedPackage")) {
            error = "The workbook is password-protected (an encrypted Excel 2007+ file); "
                    "it cannot be read without the password";
            return false;
        } else if (cfb.HasStream("WordDocument")) {
            error = "The file is a Word document, not an Excel workbook";
            return false;
        } else {
            error = "The file holds no Excel workbook stream";
            return false;
        }
    }
    if (stream.size() < 8) {
        error = "The workbook stream is empty";
        return false;
    }
    BiffParser parser(out, options);
    return parser.Parse(stream, streamBiff, error);
}

} // namespace

// ===== PUBLIC ENTRY POINTS =====

XlsFileKind DetectXlsFileKind(const std::string& filePath) {
    std::FILE* f = OpenFileUtf8(filePath, "rb");
    if (!f) return XlsFileKind::Missing;
    std::vector<uint8_t> head(4096);
    head.resize(std::fread(head.data(), 1, head.size(), f));
    std::fclose(f);
    if (UCCompoundFileReader::HasSignature(head)) return XlsFileKind::Biff;
    if (head.size() >= 4 && head[0] == 'P' && head[1] == 'K' && head[2] == 3 && head[3] == 4)
        return XlsFileKind::OpenXml;
    if (head.size() >= 2 && ((head[0] == 0xFF && head[1] == 0xFE) || (head[0] == 0xFE && head[1] == 0xFF)))
        return XlsFileKind::Text;   // UTF-16 text, as Excel saves "Unicode Text"
    if (std::find(head.begin(), head.end(), 0) != head.end()) return XlsFileKind::Unknown;
    std::string text(head.begin(), head.end());
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    size_t start = 0;
    if (text.compare(0, 3, "\xef\xbb\xbf") == 0) start = 3;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) ++start;
    if (start < text.size() && text[start] == '<') {
        if (text.find("urn:schemas-microsoft-com:office:spreadsheet", start) != std::string::npos)
            return XlsFileKind::XmlSpreadsheet;
        if (text.find("<html", start) != std::string::npos ||
            text.find("<table", start) != std::string::npos ||
            text.find("<!doctype html", start) != std::string::npos)
            return XlsFileKind::Html;
    }
    return XlsFileKind::Text;
}

bool ReadXlsWorkbook(const std::string& filePath, XlsWorkbook& out, std::string& error,
                     const XlsReadOptions& options) {
    out = XlsWorkbook{};
    error.clear();
    const XlsFileKind kind = DetectXlsFileKind(filePath);
    if (kind == XlsFileKind::Missing) {
        error = "Cannot open file: " + filePath;
        return false;
    }
    if (kind == XlsFileKind::OpenXml) {
        error = "The file is an Excel 2007+ workbook (.xlsx) under an .xls name: " + filePath;
        return false;
    }
    if (kind == XlsFileKind::Text) {
        error = "The file holds plain text, not an Excel workbook: " + filePath;
        return false;
    }
    if (kind == XlsFileKind::Unknown) {
        error = "The file is not an Excel workbook (.xls): " + filePath;
        return false;
    }
    if (kind == XlsFileKind::Html || kind == XlsFileKind::XmlSpreadsheet) {
        std::string content;
        if (!ReadWholeFile(filePath, content)) {
            error = "Cannot open file: " + filePath;
            return false;
        }
        const bool ok = kind == XlsFileKind::Html ? ReadHtmlWorkbook(content, out, options, error)
                                                  : ReadXmlSpreadsheet(content, out, options, error);
        if (!ok) error += ": " + filePath;
        return ok;
    }
    UCCompoundFileReader cfb;
    if (!cfb.Open(filePath)) {
        error = UCCompoundFileReader::HasSignature(filePath)
                    ? "The workbook's compound file is damaged: " + filePath
                    : "The file is not an Excel 97-2003 workbook (.xls): " + filePath;
        return false;
    }
    if (!ReadBiff(cfb, out, error, options)) {
        if (!error.empty()) error += ": " + filePath;
        return false;
    }
    return true;
}

bool ReadXlsWorkbookFromMemory(std::vector<uint8_t> fileBytes, XlsWorkbook& out,
                               std::string& error, const XlsReadOptions& options) {
    out = XlsWorkbook{};
    error.clear();
    UCCompoundFileReader cfb;
    if (!cfb.OpenFromMemory(std::move(fileBytes))) {
        error = "The data is not an Excel 97-2003 workbook";
        return false;
    }
    return ReadBiff(cfb, out, error, options);
}

std::string XlsErrorText(uint8_t code) {
    switch (code) {
        case 0x00: return "#NULL!";
        case 0x07: return "#DIV/0!";
        case 0x0F: return "#VALUE!";
        case 0x17: return "#REF!";
        case 0x1D: return "#NAME?";
        case 0x24: return "#NUM!";
        case 0x2A: return "#N/A";
        case 0x2B: return "#GETTING_DATA";
        default: return "#VALUE!";
    }
}

CellErrorType XlsErrorType(uint8_t code) {
    switch (code) {
        case 0x00: return CellErrorType::NullError;
        case 0x07: return CellErrorType::DivisionByZero;
        case 0x0F: return CellErrorType::ValueError;
        case 0x17: return CellErrorType::ReferenceError;
        case 0x1D: return CellErrorType::NameError;
        case 0x24: return CellErrorType::NumError;
        case 0x2A: return CellErrorType::NAError;
        case 0x2B: return CellErrorType::GettingData;
        default: return CellErrorType::ValueError;
    }
}

std::string XlsCellText(const XlsCell& cell) {
    switch (cell.type) {
        case XlsValueType::Number: return FormatFloatClassic(cell.number, 15);
        case XlsValueType::Text: return cell.text;
        case XlsValueType::Boolean: return cell.boolean ? "TRUE" : "FALSE";
        case XlsValueType::Error: return XlsErrorText(cell.errorCode);
        default: return std::string();
    }
}

NumberFormatCategory ExcelNumberFormatCategory(int numFmtId, const std::string& formatCode) {
    if ((numFmtId >= 14 && numFmtId <= 17)) return NumberFormatCategory::Date;
    if (numFmtId == 22) return NumberFormatCategory::DateTime;
    if (numFmtId >= 18 && numFmtId <= 21) return NumberFormatCategory::Time;
    if (numFmtId >= 45 && numFmtId <= 47) return NumberFormatCategory::Time;
    if (numFmtId == 9 || numFmtId == 10) return NumberFormatCategory::Percentage;
    if (numFmtId == 11 || numFmtId == 48) return NumberFormatCategory::Scientific;
    if (numFmtId == 49) return NumberFormatCategory::Text;
    if ((numFmtId >= 5 && numFmtId <= 8) || numFmtId == 42 || numFmtId == 44) {
        return NumberFormatCategory::Currency;
    }
    if (numFmtId >= 1 && numFmtId <= 4) return NumberFormatCategory::Number;
    if (numFmtId >= 37 && numFmtId <= 40) return NumberFormatCategory::Number;

    if (!formatCode.empty() && formatCode != "General") {
        // Custom code: drop quoted literals, escaped characters (\x), padding
        // (_x) and fill (*x), and the bracketed sections - a colour such as
        // [Red] would otherwise read as a day, and a locale tag such as
        // [$-409] as digits - noting what the brackets say: an elapsed time
        // ([h], [mm], [ss]) or a currency ([$EUR-407], [$\xE2\x82\xAC-x]).
        std::string code;
        bool inQuote = false;
        bool currency = false;
        bool elapsedTime = false;
        for (size_t i = 0; i < formatCode.size(); ++i) {
            const char c = formatCode[i];
            if (c == '"') { inQuote = !inQuote; continue; }
            if (c == '$' || c == '\xE2' || c == '\xC2') currency = true;
            if (inQuote) continue;
            if (c == '\\' || c == '_' || c == '*') {
                // "\$" is how LibreOffice writes a literal dollar sign.
                if (c == '\\' && i + 1 < formatCode.size()) {
                    const char next = formatCode[i + 1];
                    if (next == '$' || next == '\xE2' || next == '\xC2') currency = true;
                }
                ++i;
                continue;
            }
            if (c == '[') {
                const size_t close = formatCode.find(']', i);
                std::string inner = formatCode.substr(i + 1, close == std::string::npos
                                                                 ? std::string::npos
                                                                 : close - i - 1);
                std::transform(inner.begin(), inner.end(), inner.begin(), [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
                if (!inner.empty() && inner.find_first_not_of("hms") == std::string::npos) {
                    elapsedTime = true;
                }
                // "[$-409]" is a locale tag alone; a symbol before the '-' is a currency.
                if (inner.size() >= 2 && inner[0] == '$' && inner[1] != '-') currency = true;
                if (close == std::string::npos) break;
                i = close;
                continue;
            }
            code.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        if (elapsedTime) return NumberFormatCategory::Time;
        if (code.find('%') != std::string::npos) return NumberFormatCategory::Percentage;
        if (currency && code.find('0') != std::string::npos) return NumberFormatCategory::Currency;
        bool hasDate = code.find('y') != std::string::npos
                       || code.find('d') != std::string::npos;
        bool hasTime = code.find('h') != std::string::npos
                       || code.find('s') != std::string::npos;
        if (hasDate && hasTime) return NumberFormatCategory::DateTime;
        if (hasDate) return NumberFormatCategory::Date;
        if (hasTime) return NumberFormatCategory::Time;
        if (code.find('e') != std::string::npos
            && code.find('0') != std::string::npos) return NumberFormatCategory::Scientific;
        if (code.find('0') != std::string::npos
            || code.find('#') != std::string::npos) return NumberFormatCategory::Number;
    }
    return NumberFormatCategory::General;
}

} // namespace UltraCanvas
