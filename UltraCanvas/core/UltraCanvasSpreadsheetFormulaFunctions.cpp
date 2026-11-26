// core/UltraCanvasSpreadsheetFormulaFunctions.cpp
// The Excel-compatible part of the formula function library: lookups
// (VLOOKUP, HLOOKUP, LOOKUP, XLOOKUP, INDEX and MATCH over two dimensions),
// conditional aggregation (SUMIF(S), COUNTIF(S), AVERAGEIF(S), MAXIFS, MINIFS)
// with Excel's criteria (">5", "<>x", wildcards), SUMPRODUCT and SUBTOTAL,
// rounding and number theory, the standard statistics, text functions that
// count characters rather than bytes, dates and times, the IS* information
// functions, the newer logical functions (IFNA, IFS, SWITCH, XOR) and the
// time-value-of-money basics.
//
// A function receives its value and range arguments in two lists, each range
// flattened. What a lookup needs besides - the order the arguments were
// written in and a range's rows and columns - comes from
// FormulaEvaluator::GetCallArguments(); the CallArgs helper below hides the
// difference.
// Version: 1.1.0 - FILTER, SORT, SORTBY, UNIQUE, SEQUENCE, RANDARRAY, TRANSPOSE, XMATCH, LET, LAMBDA,
//                  MAP, BYROW, BYCOL, REDUCE, SCAN, MAKEARRAY; XLOOKUP returns rows / columns, takes
//                  match and search modes; NPER, RATE, IPMT, PPMT, CUMIPMT, CUMPRINC, IRR, XIRR,
//                  XNPV, MIRR, SLN, SYD, DB, DDB, VDB, EFFECT, NOMINAL; ApplyArrayBehaviour
// Version: 1.0.0
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework

#include "UltraCanvasSpreadsheetFormula.h"
#include "UltraCanvasTextUtils.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace UltraCanvas {

namespace {

using Values = std::vector<FormulaValue>;
using Ranges = std::vector<std::vector<FormulaValue>>;

FormulaValue Err(CellErrorType e) { return FormulaValue::Error(e); }

// A number as an int, truncated and clamped: casting 1E300 to int is
// undefined, and no argument of these functions means anything that large.
int ToInt(double v) {
    if (!std::isfinite(v)) return 0;
    return static_cast<int>(std::clamp(std::trunc(v), -2.0e9, 2.0e9));
}
FormulaValue Num(double v) {
    if (!std::isfinite(v)) return Err(CellErrorType::NumError);
    return FormulaValue::Number(v);
}

// A range argument with its shape: rows x columns, row by row.
struct Grid {
    int rows = 0;
    int cols = 0;
    Values cells;
    const FormulaValue& At(int r, int c) const { return cells[static_cast<size_t>(r * cols + c)]; }
    bool Empty() const { return cells.empty(); }
};

// The arguments of one call, in the order they were written.
class CallArgs {
public:
    CallArgs(const Values& values, const Ranges& ranges, FormulaEvaluator* evaluator)
        : values_(values), ranges_(ranges), evaluator_(evaluator) {
        if (evaluator && !evaluator->GetCallArguments().empty()) {
            order_ = evaluator->GetCallArguments();
        } else {
            // Without the evaluator's record (a direct call): values first,
            // then ranges, each range one column.
            for (size_t i = 0; i < values.size(); ++i) order_.push_back({false, i, CellRange()});
            for (size_t i = 0; i < ranges.size(); ++i) {
                FunctionCallArgument a{true, i, CellRange()};
                a.range = CellRange(0, 0, static_cast<int>(ranges[i].empty() ? 0 : ranges[i].size() - 1), 0);
                order_.push_back(a);
            }
        }
    }

    size_t Count() const { return order_.size(); }
    bool IsRange(size_t i) const { return i < order_.size() && order_[i].isRange; }

    // The argument as one value; a range gives its first cell.
    FormulaValue Value(size_t i) const {
        if (i >= order_.size()) return FormulaValue::Empty();
        const FunctionCallArgument& a = order_[i];
        if (!a.isRange) return a.index < values_.size() ? values_[a.index] : FormulaValue::Empty();
        const Values& r = ranges_[a.index];
        return r.empty() ? FormulaValue::Empty() : r.front();
    }

    double Number(size_t i, double fallback = 0.0) const {
        return i < order_.size() ? Value(i).ToNumber() : fallback;
    }
    int Int(size_t i, int fallback = 0) const {
        return i < order_.size() ? ToInt(Value(i).ToNumber()) : fallback;
    }

    // The argument as a grid; a single value is a 1 x 1 grid.
    Grid GetGrid(size_t i) const {
        Grid g;
        if (i >= order_.size()) return g;
        const FunctionCallArgument& a = order_[i];
        if (!a.isRange) {
            g.rows = g.cols = 1;
            g.cells.push_back(Value(i));
            return g;
        }
        const Values& r = ranges_[a.index];
        g.rows = a.range.end.row - a.range.start.row + 1;
        g.cols = a.range.end.col - a.range.start.col + 1;
        if (g.rows <= 0 || g.cols <= 0 ||
            static_cast<size_t>(g.rows) * static_cast<size_t>(g.cols) != r.size()) {
            // A range that did not resolve (an unknown sheet: one #REF!).
            g.rows = 1;
            g.cols = static_cast<int>(r.size());
        }
        g.cells = r;
        return g;
    }

    // Every value the argument holds, in order.
    Values Flat(size_t i) const {
        if (i >= order_.size()) return {};
        const FunctionCallArgument& a = order_[i];
        if (!a.isRange) return {Value(i)};
        return ranges_[a.index];
    }

    // The first error among the scalar arguments from `first` on.
    bool ScalarError(size_t first, FormulaValue& error) const {
        for (size_t i = first; i < order_.size(); ++i) {
            if (!order_[i].isRange && Value(i).IsError()) {
                error = Value(i);
                return true;
            }
        }
        return false;
    }

    // Whether argument i was left out (SEQUENCE(3,,10)) or not given at all.
    bool Omitted(size_t i) const {
        return i >= order_.size() || (!order_[i].isRange && Value(i).IsEmpty());
    }
    // The evaluator running the call (to call a LAMBDA argument); may be null.
    FormulaEvaluator* Evaluator() const { return evaluator_; }

private:
    const Values& values_;
    const Ranges& ranges_;
    FormulaEvaluator* evaluator_ = nullptr;
    std::vector<FunctionCallArgument> order_;
};

using Impl = std::function<FormulaValue(const CallArgs&)>;

void Add(FormulaFunctionLibrary& library, const char* name, int minArgs, int maxArgs,
         const char* category, const char* description, Impl impl, bool isVolatile = false) {
    FunctionDefinition def;
    def.name = name;
    def.minArgs = minArgs;
    def.maxArgs = maxArgs;
    def.isVolatile = isVolatile;
    def.category = category;
    def.description = description;
    def.implementation = [impl, minArgs, maxArgs](const Values& values, const Ranges& ranges,
                                                  FormulaEvaluator* evaluator) -> FormulaValue {
        CallArgs args(values, ranges, evaluator);
        const int count = static_cast<int>(args.Count());
        if (count < minArgs || (maxArgs >= 0 && count > maxArgs)) {
            return Err(CellErrorType::ValueError);
        }
        return impl(args);
    };
    library.RegisterFunction(def);
}

// ===== TEXT HELPERS (UTF-8 aware) =====

std::string Lower(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

// Byte offset of every character start, plus the end.
std::vector<size_t> CharOffsets(const std::string& s) {
    std::vector<size_t> offsets;
    for (size_t i = 0; i < s.size(); ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) offsets.push_back(i);
    }
    offsets.push_back(s.size());
    return offsets;
}

size_t CharCount(const std::string& s) { return CharOffsets(s).size() - 1; }

// Characters [start, start + count) of `s` (0-based, clamped).
std::string CharSlice(const std::string& s, size_t start, size_t count) {
    const std::vector<size_t> o = CharOffsets(s);
    const size_t n = o.size() - 1;
    if (start >= n) return std::string();
    const size_t end = std::min(n, start + count);
    return s.substr(o[start], o[end] - o[start]);
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

uint32_t FirstCodePoint(const std::string& s) {
    if (s.empty()) return 0;
    const unsigned char c = static_cast<unsigned char>(s[0]);
    if (c < 0x80) return c;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
    uint32_t cp = c & (0x3F >> extra);
    for (int i = 1; i <= extra && static_cast<size_t>(i) < s.size(); ++i) {
        cp = (cp << 6) | (static_cast<unsigned char>(s[static_cast<size_t>(i)]) & 0x3F);
    }
    return cp;
}

// Excel's wildcards: * any run, ? one character, ~ escapes the next.
// Case-insensitive (ASCII), character-wise over UTF-8.
bool WildcardMatch(const std::string& pattern, const std::string& text) {
    const std::string p = Lower(pattern), t = Lower(text);
    const std::vector<size_t> po = CharOffsets(p), to = CharOffsets(t);
    const size_t pn = po.size() - 1, tn = to.size() - 1;
    auto pchar = [&](size_t i) { return p.substr(po[i], po[i + 1] - po[i]); };
    auto tchar = [&](size_t i) { return t.substr(to[i], to[i + 1] - to[i]); };
    // Iterative glob with one backtrack point.
    size_t pi = 0, ti = 0, starP = std::string::npos, starT = 0;
    while (ti < tn) {
        if (pi < pn) {
            const std::string pc = pchar(pi);
            if (pc == "*") { starP = pi++; starT = ti; continue; }
            if (pc == "?") { ++pi; ++ti; continue; }
            std::string literal = pc;
            size_t step = 1;
            if (pc == "~" && pi + 1 < pn) { literal = pchar(pi + 1); step = 2; }
            if (literal == tchar(ti)) { pi += step; ++ti; continue; }
        }
        if (starP == std::string::npos) return false;
        pi = starP + 1;
        ti = ++starT;
    }
    while (pi < pn && pchar(pi) == "*") ++pi;
    return pi == pn;
}

bool HasWildcard(const std::string& s) { return s.find_first_of("*?~") != std::string::npos; }

// ===== COMPARISON AND CRITERIA =====

// Excel's ordering across types: numbers < text < booleans; text compares
// without regard to case. Returns <0, 0, >0. Empty counts as 0 / "".
int CompareValues(const FormulaValue& a, const FormulaValue& b) {
    auto rank = [](const FormulaValue& v) {
        if (v.IsNumber() || v.IsEmpty()) return 0;
        if (v.IsText()) return 1;
        if (v.IsBoolean()) return 2;
        return 3;
    };
    const int ra = rank(a), rb = rank(b);
    if (ra != rb) return ra < rb ? -1 : 1;
    if (ra == 0) {
        const double x = a.IsEmpty() ? 0.0 : a.GetNumber(), y = b.IsEmpty() ? 0.0 : b.GetNumber();
        return x < y ? -1 : x > y ? 1 : 0;
    }
    if (ra == 1) {
        const int c = Lower(a.GetText()).compare(Lower(b.GetText()));
        return c < 0 ? -1 : c > 0 ? 1 : 0;
    }
    if (ra == 2) return static_cast<int>(a.GetBoolean()) - static_cast<int>(b.GetBoolean());
    return 0;
}

bool SameValue(const FormulaValue& lookup, const FormulaValue& cell, bool wildcards) {
    if (lookup.IsText() && cell.IsText()) {
        if (wildcards && HasWildcard(lookup.GetText())) return WildcardMatch(lookup.GetText(), cell.GetText());
        return Lower(lookup.GetText()) == Lower(cell.GetText());
    }
    if (lookup.IsNumber() && cell.IsNumber()) return lookup.GetNumber() == cell.GetNumber();
    if (lookup.IsBoolean() && cell.IsBoolean()) return lookup.GetBoolean() == cell.GetBoolean();
    return false;
}

// One criterion of SUMIF / COUNTIF and their kin: 5, ">5", "<>apple",
// "a*", "=" (empty), "<>" (not empty).
class Criterion {
public:
    explicit Criterion(const FormulaValue& c) {
        if (c.IsNumber()) { kind_ = Kind::Number; op_ = "="; number_ = c.GetNumber(); return; }
        if (c.IsBoolean()) { kind_ = Kind::Boolean; op_ = "="; boolean_ = c.GetBoolean(); return; }
        if (c.IsError()) { kind_ = Kind::Error; error_ = c.GetError(); return; }
        std::string s = c.GetText();
        for (const char* op : {"<=", ">=", "<>", "<", ">", "="}) {
            const size_t n = std::char_traits<char>::length(op);
            if (s.compare(0, n, op) == 0) { op_ = op; s = s.substr(n); break; }
        }
        if (op_.empty()) op_ = "=";
        double v = 0.0;
        const char* first = s.c_str();
        const char* last = first + s.size();
        if (!s.empty() && ParseFloatClassic(first, last, v) == last) {
            kind_ = Kind::Number;
            number_ = v;
        } else if (Lower(s) == "true" || Lower(s) == "false") {
            kind_ = Kind::Boolean;
            boolean_ = Lower(s) == "true";
        } else {
            kind_ = Kind::Text;
            text_ = s;
        }
    }

    bool Matches(const FormulaValue& v) const {
        switch (kind_) {
            case Kind::Error:
                return v.IsError() && v.GetError() == error_;
            case Kind::Boolean:
                if (!v.IsBoolean()) return op_ == "<>";
                return Compare(static_cast<int>(v.GetBoolean()) - static_cast<int>(boolean_));
            case Kind::Number: {
                double n = 0.0;
                if (v.IsNumber()) {
                    n = v.GetNumber();
                } else if (v.IsText()) {
                    // "5" in a cell matches the criterion 5 for = and <>.
                    const std::string t = v.GetText();
                    const char* first = t.c_str();
                    const char* last = first + t.size();
                    if (t.empty() || ParseFloatClassic(first, last, n) != last) return op_ == "<>";
                    if (op_ != "=" && op_ != "<>") return false;
                } else {
                    return op_ == "<>";
                }
                return Compare(n < number_ ? -1 : n > number_ ? 1 : 0);
            }
            case Kind::Text: {
                if (text_.empty()) {
                    const bool empty = v.IsEmpty() || (v.IsText() && v.GetText().empty());
                    return op_ == "<>" ? !empty : op_ == "=" ? empty : false;
                }
                if (!v.IsText()) return op_ == "<>";
                if (op_ == "=" || op_ == "<>") {
                    const bool same = HasWildcard(text_) ? WildcardMatch(text_, v.GetText())
                                                         : Lower(text_) == Lower(v.GetText());
                    return op_ == "=" ? same : !same;
                }
                const int c = Lower(v.GetText()).compare(Lower(text_));
                return Compare(c < 0 ? -1 : c > 0 ? 1 : 0);
            }
        }
        return false;
    }

private:
    enum class Kind { Number, Text, Boolean, Error };
    Kind kind_ = Kind::Text;
    std::string op_;
    double number_ = 0.0;
    bool boolean_ = false;
    std::string text_;
    CellErrorType error_ = CellErrorType::NoError;

    bool Compare(int c) const {
        if (op_ == "=") return c == 0;
        if (op_ == "<>") return c != 0;
        if (op_ == "<") return c < 0;
        if (op_ == ">") return c > 0;
        if (op_ == "<=") return c <= 0;
        return c >= 0;   // ">="
    }
};

// The cells of `target` whose counterparts in every (range, criterion) pair
// match; the pairs start at argument `first`. False (with #VALUE!) when the
// ranges differ in size.
bool CriteriaMask(const CallArgs& a, size_t first, size_t cells, std::vector<bool>& mask,
                  FormulaValue& error) {
    mask.assign(cells, true);
    for (size_t i = first; i + 1 < a.Count(); i += 2) {
        const Values range = a.Flat(i);
        if (range.size() != cells) {
            error = Err(CellErrorType::ValueError);
            return false;
        }
        const Criterion criterion(a.Value(i + 1));
        for (size_t k = 0; k < cells; ++k) {
            if (mask[k] && !criterion.Matches(range[k])) mask[k] = false;
        }
    }
    return true;
}

// ===== NUMBERS =====

// Numbers to aggregate: from ranges only real numbers; a value written
// directly counts when it reads as a number (TRUE = 1, "5" = 5).
bool CollectNumbers(const CallArgs& a, size_t first, std::vector<double>& out, FormulaValue& error) {
    for (size_t i = first; i < a.Count(); ++i) {
        if (a.IsRange(i)) {
            for (const FormulaValue& v : a.Flat(i)) {
                if (v.IsError()) { error = v; return false; }
                if (v.IsNumber()) out.push_back(v.GetNumber());
            }
        } else {
            const FormulaValue v = a.Value(i);
            if (v.IsError()) { error = v; return false; }
            if (v.IsEmpty()) continue;
            if (v.IsNumber() || v.IsBoolean()) {
                out.push_back(v.ToNumber());
            } else {
                double n = 0.0;
                const std::string t = v.GetText();
                const char* f = t.c_str();
                if (t.empty() || ParseFloatClassic(f, f + t.size(), n) != f + t.size()) {
                    error = Err(CellErrorType::ValueError);
                    return false;
                }
                out.push_back(n);
            }
        }
    }
    return true;
}

double RoundTo(double v, int digits, int mode) {   // mode: 0 nearest, 1 up (away), -1 down (toward 0)
    const double m = std::pow(10.0, digits);
    // Counter the binary representation (2.675 is 2.67499999...): the scaled
    // value is first rounded to 12 significant decimals.
    double scaled = v * m;
    const double magnitude = std::fabs(scaled);
    if (magnitude > 0 && magnitude < 1e15) {
        const double guard = std::pow(10.0, 12 - static_cast<int>(std::floor(std::log10(magnitude))) - 1);
        scaled = std::round(scaled * guard) / guard;
    }
    double r;
    if (mode == 0) r = std::round(scaled);
    else if (mode > 0) r = scaled < 0 ? -std::ceil(-scaled) : std::ceil(scaled);
    else r = std::trunc(scaled);
    return r / m;
}

double Variance(const std::vector<double>& v, bool sample) {
    double mean = 0.0;
    for (double x : v) mean += x;
    mean /= static_cast<double>(v.size());
    double s = 0.0;
    for (double x : v) s += (x - mean) * (x - mean);
    return s / static_cast<double>(sample ? v.size() - 1 : v.size());
}

double Percentile(std::vector<double> v, double k) {
    std::sort(v.begin(), v.end());
    const double h = (static_cast<double>(v.size()) - 1.0) * k;
    const size_t lo = static_cast<size_t>(std::floor(h));
    const size_t hi = std::min(v.size() - 1, lo + 1);
    return v[lo] + (h - std::floor(h)) * (v[hi] - v[lo]);
}

// ===== DATES (serials, 0 = 1899-12-30) =====

void SerialToDate(double serial, int& y, int& m, int& d) {
    DateTimeValue(std::floor(serial)).ToDate(y, m, d);
}

double DateToSerial(int y, int m, int d) { return DateTimeValue::FromDate(y, m, d).serialNumber; }

int DaysInMonth(int y, int m) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return days[(m - 1 + 12) % 12];
}

bool ValidSerial(double s) { return std::isfinite(s) && s >= 0 && s < 2958466.0; }

// The date `months` after the serial's, its day clamped to the month.
bool AddMonths(double serial, int months, bool endOfMonth, double& out) {
    if (!ValidSerial(serial) || months < -120000 || months > 120000) return false;
    int y, m, d;
    SerialToDate(serial, y, m, d);
    long total = static_cast<long>(y) * 12 + (m - 1) + months;
    y = static_cast<int>(total / 12);
    m = static_cast<int>(total % 12) + 1;
    if (y < 1900 || y > 9999) return false;
    const int last = DaysInMonth(y, m);
    out = DateToSerial(y, m, endOfMonth ? last : std::min(d, last));
    return true;
}

// ===== LOOKUPS =====

// Position of `lookup` in a line of cells: exact (with wildcards), the
// largest value <= lookup in an ascending line (approximate = 1), or the
// smallest >= lookup in a descending one (-1). -1 when not found.
int FindInLine(const FormulaValue& lookup, const Values& line, int matchType) {
    if (matchType == 0) {
        for (size_t i = 0; i < line.size(); ++i) {
            if (SameValue(lookup, line[i], true)) return static_cast<int>(i);
        }
        return -1;
    }
    int best = -1;
    for (size_t i = 0; i < line.size(); ++i) {
        const FormulaValue& v = line[i];
        if (v.IsEmpty() || v.IsError()) continue;
        const bool comparable = (v.IsNumber() && lookup.IsNumber()) || (v.IsText() && lookup.IsText()) ||
                                (v.IsBoolean() && lookup.IsBoolean());
        if (!comparable) continue;
        const int c = CompareValues(v, lookup);
        if (matchType > 0) {
            if (c <= 0) best = static_cast<int>(i);
            else break;   // ascending: nothing later is smaller
        } else {
            if (c >= 0) best = static_cast<int>(i);
            else break;
        }
    }
    return best;
}

Values Column(const Grid& g, int c) {
    Values out;
    for (int r = 0; r < g.rows; ++r) out.push_back(g.At(r, c));
    return out;
}

Values Row(const Grid& g, int r) {
    Values out;
    for (int c = 0; c < g.cols; ++c) out.push_back(g.At(r, c));
    return out;
}

// A grid as a value: an array, or the one value it holds.
FormulaValue GridValue(const Grid& g) {
    if (g.Empty()) return FormulaValue::Empty();
    return FormulaValue::Array(g.rows, g.cols, g.cells);
}

// XLOOKUP / XMATCH: the position of `lookup` in `line`. matchMode 0 exact,
// -1 exact or the next smaller, 1 exact or the next larger, 2 wildcards;
// searchMode 1 / 2 from the first, -1 / -2 from the last (2 and -2, a binary
// search over sorted data in Excel, find the same item). -1 when none.
int XFind(const FormulaValue& lookup, const Values& line, int matchMode, int searchMode) {
    const int n = static_cast<int>(line.size());
    const bool backwards = searchMode < 0;
    int best = -1;
    for (int k = 0; k < n; ++k) {
        const int i = backwards ? n - 1 - k : k;
        const FormulaValue& v = line[static_cast<size_t>(i)];
        if (SameValue(lookup, v, matchMode == 2)) return i;
        if (matchMode != 1 && matchMode != -1) continue;
        const bool comparable = (v.IsNumber() && lookup.IsNumber()) || (v.IsText() && lookup.IsText()) ||
                                (v.IsBoolean() && lookup.IsBoolean());
        if (!comparable) continue;
        const int c = CompareValues(v, lookup);
        if ((matchMode == -1 && c > 0) || (matchMode == 1 && c < 0)) continue;
        if (best < 0) { best = i; continue; }
        // The closest; of equally close ones the first met.
        const int closer = CompareValues(v, line[static_cast<size_t>(best)]);
        if ((matchMode == -1 && closer > 0) || (matchMode == 1 && closer < 0)) best = i;
    }
    return best;
}

// ===== REGISTRATION =====

void RegisterLookups(FormulaFunctionLibrary& lib) {
    auto vlookup = [](bool vertical) {
        return [vertical](const CallArgs& a) -> FormulaValue {
            FormulaValue error;
            if (a.ScalarError(0, error)) return error;
            const FormulaValue lookup = a.Value(0);
            const Grid table = a.GetGrid(1);
            const int index = a.Int(2);
            const bool approximate = a.Count() > 3 ? a.Value(3).ToNumber() != 0.0 : true;
            if (index < 1) return Err(CellErrorType::ValueError);
            if (table.Empty()) return Err(CellErrorType::NAError);
            if (index > (vertical ? table.cols : table.rows)) return Err(CellErrorType::ReferenceError);
            const Values keys = vertical ? Column(table, 0) : Row(table, 0);
            const int pos = FindInLine(lookup, keys, approximate ? 1 : 0);
            if (pos < 0) return Err(CellErrorType::NAError);
            return vertical ? table.At(pos, index - 1) : table.At(index - 1, pos);
        };
    };
    Add(lib, "VLOOKUP", 3, 4, "Lookup", "Look up a value in the first column of a table", vlookup(true));
    Add(lib, "HLOOKUP", 3, 4, "Lookup", "Look up a value in the first row of a table", vlookup(false));

    Add(lib, "LOOKUP", 2, 3, "Lookup", "Approximate lookup in a sorted vector",
        [](const CallArgs& a) -> FormulaValue {
            FormulaValue error;
            if (a.ScalarError(0, error)) return error;
            const Grid keys = a.GetGrid(1);
            if (keys.Empty()) return Err(CellErrorType::NAError);
            // Array form: search the first row or column, return from the last.
            const bool tall = keys.rows >= keys.cols;
            const Values line = tall ? Column(keys, 0) : Row(keys, 0);
            const int pos = FindInLine(a.Value(0), line, 1);
            if (pos < 0) return Err(CellErrorType::NAError);
            if (a.Count() > 2) {
                const Values result = a.Flat(2);
                return static_cast<size_t>(pos) < result.size() ? result[static_cast<size_t>(pos)]
                                                                : Err(CellErrorType::NAError);
            }
            return tall ? keys.At(pos, keys.cols - 1) : keys.At(keys.rows - 1, pos);
        });

    // XLOOKUP(lookup_value, lookup_array, return_array, [if_not_found],
    // [match_mode], [search_mode]): a row of return_array for a lookup down a
    // column, a column of it for a lookup along a row; a range of lookup
    // values gives one result each.
    Add(lib, "XLOOKUP", 3, 6, "Lookup", "Look up a value and return the matching item",
        [](const CallArgs& a) -> FormulaValue {
            const Grid keys = a.GetGrid(1);
            const Grid results = a.GetGrid(2);
            if (keys.Empty() || results.Empty() || (keys.rows != 1 && keys.cols != 1)) {
                return Err(CellErrorType::ValueError);
            }
            const bool down = keys.cols == 1 && !(keys.rows == 1 && results.rows == 1);
            const int n = down ? keys.rows : keys.cols;
            if ((down ? results.rows : results.cols) != n) return Err(CellErrorType::ValueError);
            const int mode = a.Omitted(4) ? 0 : a.Int(4);
            const int search = a.Omitted(5) ? 1 : a.Int(5);
            if (mode < -1 || mode > 2 || (search != 1 && search != -1 && search != 2 && search != -2)) {
                return Err(CellErrorType::ValueError);
            }
            auto one = [&](const FormulaValue& lookup) -> FormulaValue {
                if (lookup.IsError()) return lookup;
                const int pos = XFind(lookup, keys.cells, mode, search);
                if (pos < 0) {
                    if (a.Count() > 3 && !a.Omitted(3)) return GridValue(a.GetGrid(3));
                    return Err(CellErrorType::NAError);
                }
                if (down) {
                    Values row;
                    for (int c = 0; c < results.cols; ++c) row.push_back(results.At(pos, c));
                    return FormulaValue::Array(1, results.cols, std::move(row));
                }
                Values column;
                for (int r = 0; r < results.rows; ++r) column.push_back(results.At(r, pos));
                return FormulaValue::Array(results.rows, 1, std::move(column));
            };
            const Grid lookups = a.GetGrid(0);
            if (lookups.cells.size() <= 1) return one(a.Value(0));
            Values out;
            for (const FormulaValue& v : lookups.cells) out.push_back(one(v).TopLeft());
            return FormulaValue::Array(lookups.rows, lookups.cols, std::move(out));
        });

    // XMATCH(lookup_value, lookup_array, [match_mode], [search_mode]).
    Add(lib, "XMATCH", 2, 4, "Lookup", "Position of a value in a row or column",
        [](const CallArgs& a) -> FormulaValue {
            const Grid keys = a.GetGrid(1);
            if (keys.Empty() || (keys.rows != 1 && keys.cols != 1)) return Err(CellErrorType::ValueError);
            const int mode = a.Omitted(2) ? 0 : a.Int(2);
            const int search = a.Omitted(3) ? 1 : a.Int(3);
            if (mode < -1 || mode > 2 || (search != 1 && search != -1 && search != 2 && search != -2)) {
                return Err(CellErrorType::ValueError);
            }
            auto one = [&](const FormulaValue& lookup) -> FormulaValue {
                if (lookup.IsError()) return lookup;
                const int pos = XFind(lookup, keys.cells, mode, search);
                return pos < 0 ? Err(CellErrorType::NAError) : Num(pos + 1);
            };
            const Grid lookups = a.GetGrid(0);
            if (lookups.cells.size() <= 1) return one(a.Value(0));
            Values out;
            for (const FormulaValue& v : lookups.cells) out.push_back(one(v));
            return FormulaValue::Array(lookups.rows, lookups.cols, std::move(out));
        });

    Add(lib, "INDEX", 2, 3, "Lookup", "The value at a row and column of a range",
        [](const CallArgs& a) -> FormulaValue {
            const Grid g = a.GetGrid(0);
            if (g.Empty()) return Err(CellErrorType::ReferenceError);
            int row = a.Int(1);
            int col = a.Count() > 2 ? a.Int(2) : 0;
            // One row or one column: the single index walks along it.
            if (a.Count() == 2) {
                if (g.rows == 1) { col = row; row = 1; }
                else if (g.cols == 1) col = 1;
            }
            if (row == 0 && g.rows == 1) row = 1;
            if (col == 0 && g.cols == 1) col = 1;
            if (row < 1 || col < 1) return Err(CellErrorType::ValueError);
            if (row > g.rows || col > g.cols) return Err(CellErrorType::ReferenceError);
            return g.At(row - 1, col - 1);
        });

    Add(lib, "MATCH", 2, 3, "Lookup", "Position of a value in a row or column",
        [](const CallArgs& a) -> FormulaValue {
            FormulaValue error;
            if (a.ScalarError(0, error)) return error;
            const Values line = a.Flat(1);
            const int type = a.Count() > 2 ? a.Int(2) : 1;
            const int pos = FindInLine(a.Value(0), line, type > 0 ? 1 : type < 0 ? -1 : 0);
            if (pos < 0) return Err(CellErrorType::NAError);
            return Num(pos + 1);
        });

    Add(lib, "CHOOSE", 2, 255, "Lookup", "The n-th of a list of values",
        [](const CallArgs& a) -> FormulaValue {
            const FormulaValue index = a.Value(0);
            if (index.IsError()) return index;
            const int n = ToInt(index.ToNumber());
            if (n < 1 || static_cast<size_t>(n) >= a.Count()) return Err(CellErrorType::ValueError);
            return a.Value(static_cast<size_t>(n));
        });

    Add(lib, "ROWS", 1, 1, "Lookup", "Number of rows in a range",
        [](const CallArgs& a) -> FormulaValue { return Num(a.GetGrid(0).rows); });
    Add(lib, "COLUMNS", 1, 1, "Lookup", "Number of columns in a range",
        [](const CallArgs& a) -> FormulaValue { return Num(a.GetGrid(0).cols); });
}

void RegisterConditional(FormulaFunctionLibrary& lib) {
    // SUMIF(range, criterion, [sum_range]) and AVERAGEIF(...).
    auto sumIf = [](bool average) {
        return [average](const CallArgs& a) -> FormulaValue {
            const Values range = a.Flat(0);
            const Criterion criterion(a.Value(1));
            const Values target = a.Count() > 2 ? a.Flat(2) : range;
            double sum = 0.0;
            int count = 0;
            for (size_t k = 0; k < range.size() && k < target.size(); ++k) {
                if (!criterion.Matches(range[k])) continue;
                if (target[k].IsError()) return target[k];
                if (!target[k].IsNumber()) continue;
                sum += target[k].GetNumber();
                ++count;
            }
            if (average) return count ? Num(sum / count) : Err(CellErrorType::DivisionByZero);
            return Num(sum);
        };
    };
    Add(lib, "SUMIF", 2, 3, "Math", "Sum of the cells that meet a criterion", sumIf(false));
    Add(lib, "AVERAGEIF", 2, 3, "Statistical", "Average of the cells that meet a criterion", sumIf(true));

    Add(lib, "COUNTIF", 2, 2, "Statistical", "Number of cells that meet a criterion",
        [](const CallArgs& a) -> FormulaValue {
            const Criterion criterion(a.Value(1));
            int count = 0;
            for (const FormulaValue& v : a.Flat(0)) {
                if (criterion.Matches(v)) ++count;
            }
            return Num(count);
        });

    Add(lib, "COUNTIFS", 2, 254, "Statistical", "Number of cells that meet every criterion",
        [](const CallArgs& a) -> FormulaValue {
            if (a.Count() % 2 != 0) return Err(CellErrorType::ValueError);
            std::vector<bool> mask;
            FormulaValue error;
            if (!CriteriaMask(a, 0, a.Flat(0).size(), mask, error)) return error;
            return Num(static_cast<double>(std::count(mask.begin(), mask.end(), true)));
        });

    // SUMIFS / AVERAGEIFS / MAXIFS / MINIFS (target, range1, crit1, ...).
    auto ifs = [](int kind) {   // 0 sum, 1 average, 2 max, 3 min
        return [kind](const CallArgs& a) -> FormulaValue {
            if (a.Count() % 2 == 0) return Err(CellErrorType::ValueError);
            const Values target = a.Flat(0);
            std::vector<bool> mask;
            FormulaValue error;
            if (!CriteriaMask(a, 1, target.size(), mask, error)) return error;
            double acc = kind == 2 ? -INFINITY : kind == 3 ? INFINITY : 0.0;
            int count = 0;
            for (size_t k = 0; k < target.size(); ++k) {
                if (!mask[k]) continue;
                if (target[k].IsError()) return target[k];
                if (!target[k].IsNumber()) continue;
                const double v = target[k].GetNumber();
                if (kind == 2) acc = std::max(acc, v);
                else if (kind == 3) acc = std::min(acc, v);
                else acc += v;
                ++count;
            }
            if (kind == 1) return count ? Num(acc / count) : Err(CellErrorType::DivisionByZero);
            if (kind >= 2) return Num(count ? acc : 0.0);
            return Num(acc);
        };
    };
    Add(lib, "SUMIFS", 3, 255, "Math", "Sum of the cells that meet every criterion", ifs(0));
    Add(lib, "AVERAGEIFS", 3, 255, "Statistical", "Average of the cells that meet every criterion", ifs(1));
    Add(lib, "MAXIFS", 3, 255, "Statistical", "Largest of the cells that meet every criterion", ifs(2));
    Add(lib, "MINIFS", 3, 255, "Statistical", "Smallest of the cells that meet every criterion", ifs(3));

    Add(lib, "COUNTBLANK", 1, 1, "Statistical", "Number of empty cells",
        [](const CallArgs& a) -> FormulaValue {
            int count = 0;
            for (const FormulaValue& v : a.Flat(0)) {
                if (v.IsEmpty() || (v.IsText() && v.GetText().empty())) ++count;
            }
            return Num(count);
        });

    Add(lib, "SUMPRODUCT", 1, 255, "Math", "Sum of the products of corresponding cells",
        [](const CallArgs& a) -> FormulaValue {
            std::vector<Values> arrays;
            for (size_t i = 0; i < a.Count(); ++i) arrays.push_back(a.Flat(i));
            const size_t n = arrays.front().size();
            for (const Values& arr : arrays) {
                if (arr.size() != n) return Err(CellErrorType::ValueError);
            }
            double sum = 0.0;
            for (size_t k = 0; k < n; ++k) {
                double product = 1.0;
                for (const Values& arr : arrays) {
                    if (arr[k].IsError()) return arr[k];
                    product *= arr[k].IsNumber() ? arr[k].GetNumber() : 0.0;
                }
                sum += product;
            }
            return Num(sum);
        });

    Add(lib, "SUBTOTAL", 2, 255, "Math", "AVERAGE, COUNT, SUM, ... of ranges by function number",
        [](const CallArgs& a) -> FormulaValue {
            int fn = a.Int(0);
            if (fn > 100) fn -= 100;   // 101-111: the same, ignoring hidden rows
            std::vector<double> nums;
            int countA = 0;
            for (size_t i = 1; i < a.Count(); ++i) {
                for (const FormulaValue& v : a.Flat(i)) {
                    if (v.IsError()) return v;
                    if (v.IsNumber()) nums.push_back(v.GetNumber());
                    if (!v.IsEmpty()) ++countA;
                }
            }
            double sum = 0.0;
            for (double v : nums) sum += v;
            switch (fn) {
                case 1: return nums.empty() ? Err(CellErrorType::DivisionByZero) : Num(sum / nums.size());
                case 2: return Num(static_cast<double>(nums.size()));
                case 3: return Num(countA);
                case 4: return Num(nums.empty() ? 0.0 : *std::max_element(nums.begin(), nums.end()));
                case 5: return Num(nums.empty() ? 0.0 : *std::min_element(nums.begin(), nums.end()));
                case 6: {
                    double p = 1.0;
                    for (double v : nums) p *= v;
                    return Num(nums.empty() ? 0.0 : p);
                }
                case 7: return nums.size() < 2 ? Err(CellErrorType::DivisionByZero) : Num(std::sqrt(Variance(nums, true)));
                case 8: return nums.empty() ? Err(CellErrorType::DivisionByZero) : Num(std::sqrt(Variance(nums, false)));
                case 9: return Num(sum);
                case 10: return nums.size() < 2 ? Err(CellErrorType::DivisionByZero) : Num(Variance(nums, true));
                case 11: return nums.empty() ? Err(CellErrorType::DivisionByZero) : Num(Variance(nums, false));
                default: return Err(CellErrorType::ValueError);
            }
        });
}

void RegisterMath(FormulaFunctionLibrary& lib) {
    auto rounding = [](int mode) {
        return [mode](const CallArgs& a) -> FormulaValue {
            FormulaValue error;
            if (a.ScalarError(0, error)) return error;
            const int digits = a.Count() > 1 ? std::clamp(a.Int(1), -308, 308) : 0;
            return Num(RoundTo(a.Number(0), digits, mode));
        };
    };
    Add(lib, "ROUNDUP", 2, 2, "Math", "Round away from zero", rounding(1));
    Add(lib, "ROUNDDOWN", 2, 2, "Math", "Round toward zero", rounding(-1));
    Add(lib, "TRUNC", 1, 2, "Math", "Cut off the decimals", rounding(-1));

    auto multiple = [](int mode) {   // 1 ceiling, -1 floor, 0 nearest (MROUND)
        return [mode](const CallArgs& a) -> FormulaValue {
            FormulaValue error;
            if (a.ScalarError(0, error)) return error;
            const double n = a.Number(0);
            const double sig = a.Count() > 1 ? a.Number(1) : 1.0;
            if (sig == 0) return mode < 0 ? Err(CellErrorType::DivisionByZero) : Num(0);
            if (n > 0 && sig < 0) return Err(CellErrorType::NumError);
            if (mode == 0 && n < 0 && sig > 0) return Err(CellErrorType::NumError);
            const double q = n / sig;
            const double guarded = std::round(q * 1e9) / 1e9;   // 0.3/0.1 is 2.9999999
            const double k = mode > 0 ? std::ceil(guarded) : mode < 0 ? std::floor(guarded)
                                                                     : std::round(guarded);
            return Num(k * sig);
        };
    };
    Add(lib, "CEILING", 1, 2, "Math", "Round up to a multiple", multiple(1));
    Add(lib, "FLOOR", 1, 2, "Math", "Round down to a multiple", multiple(-1));
    Add(lib, "MROUND", 2, 2, "Math", "Round to the nearest multiple", multiple(0));

    Add(lib, "SIGN", 1, 1, "Math", "Sign of a number", [](const CallArgs& a) -> FormulaValue {
        const FormulaValue v = a.Value(0);
        if (v.IsError()) return v;
        const double n = v.ToNumber();
        return Num(n > 0 ? 1 : n < 0 ? -1 : 0);
    });
    auto evenOdd = [](bool odd) {
        return [odd](const CallArgs& a) -> FormulaValue {
            const FormulaValue v = a.Value(0);
            if (v.IsError()) return v;
            const double n = v.ToNumber();
            const double sign = n < 0 ? -1.0 : 1.0;
            double m = std::ceil(std::fabs(n));
            if (odd) {
                if (std::fmod(m, 2.0) == 0.0) m += 1.0;
            } else if (std::fmod(m, 2.0) != 0.0) {
                m += 1.0;
            }
            return Num(sign * m);
        };
    };
    Add(lib, "EVEN", 1, 1, "Math", "Round up to an even integer", evenOdd(false));
    Add(lib, "ODD", 1, 1, "Math", "Round up to an odd integer", evenOdd(true));

    Add(lib, "PRODUCT", 1, 255, "Math", "Product of numbers", [](const CallArgs& a) -> FormulaValue {
        std::vector<double> v;
        FormulaValue error;
        if (!CollectNumbers(a, 0, v, error)) return error;
        if (v.empty()) return Num(0);
        double p = 1.0;
        for (double x : v) p *= x;
        return Num(p);
    });
    Add(lib, "SUMSQ", 1, 255, "Math", "Sum of squares", [](const CallArgs& a) -> FormulaValue {
        std::vector<double> v;
        FormulaValue error;
        if (!CollectNumbers(a, 0, v, error)) return error;
        double s = 0.0;
        for (double x : v) s += x * x;
        return Num(s);
    });
    Add(lib, "QUOTIENT", 2, 2, "Math", "Integer part of a division", [](const CallArgs& a) -> FormulaValue {
        FormulaValue error;
        if (a.ScalarError(0, error)) return error;
        const double d = a.Number(1);
        if (d == 0) return Err(CellErrorType::DivisionByZero);
        return Num(std::trunc(a.Number(0) / d));
    });
    Add(lib, "FACT", 1, 1, "Math", "Factorial", [](const CallArgs& a) -> FormulaValue {
        const double n = std::trunc(a.Number(0));
        if (n < 0 || n > 170) return Err(CellErrorType::NumError);
        double f = 1.0;
        for (int i = 2; i <= static_cast<int>(n); ++i) f *= i;
        return Num(f);
    });
    Add(lib, "COMBIN", 2, 2, "Math", "Number of combinations", [](const CallArgs& a) -> FormulaValue {
        const double n = std::trunc(a.Number(0)), k = std::trunc(a.Number(1));
        if (!std::isfinite(n) || n < 0 || k < 0 || k > n) return Err(CellErrorType::NumError);
        const double m = std::min(k, n - k);
        // C(2m, m) passes the double range from m = 515 on.
        if (m > 1100) return Err(CellErrorType::NumError);
        double r = 1.0;
        for (double i = 1; i <= m; ++i) r = r * (n - m + i) / i;
        return Num(std::round(r));
    });
    auto gcdOf = [](double x, double y) {
        while (y != 0) { const double t = std::fmod(x, y); x = y; y = t; }
        return x;
    };
    Add(lib, "GCD", 1, 255, "Math", "Greatest common divisor", [gcdOf](const CallArgs& a) -> FormulaValue {
        std::vector<double> v;
        FormulaValue error;
        if (!CollectNumbers(a, 0, v, error)) return error;
        double g = 0.0;
        for (double x : v) {
            if (x < 0) return Err(CellErrorType::NumError);
            g = gcdOf(g, std::trunc(x));
        }
        return Num(g);
    });
    Add(lib, "LCM", 1, 255, "Math", "Least common multiple", [gcdOf](const CallArgs& a) -> FormulaValue {
        std::vector<double> v;
        FormulaValue error;
        if (!CollectNumbers(a, 0, v, error)) return error;
        double l = 1.0;
        for (double x : v) {
            x = std::trunc(x);
            if (x < 0) return Err(CellErrorType::NumError);
            if (x == 0) return Num(0);
            l = l / gcdOf(l, x) * x;
        }
        return Num(l);
    });
    auto unary = [](double (*fn)(double), double lo, double hi) {
        return [fn, lo, hi](const CallArgs& a) -> FormulaValue {
            const FormulaValue v = a.Value(0);
            if (v.IsError()) return v;
            const double x = v.ToNumber();
            if (x < lo || x > hi) return Err(CellErrorType::NumError);
            return Num(fn(x));
        };
    };
    const double inf = INFINITY;
    Add(lib, "ASIN", 1, 1, "Math", "Arcsine", unary([](double x) { return std::asin(x); }, -1, 1));
    Add(lib, "ACOS", 1, 1, "Math", "Arccosine", unary([](double x) { return std::acos(x); }, -1, 1));
    Add(lib, "ATAN", 1, 1, "Math", "Arctangent", unary([](double x) { return std::atan(x); }, -inf, inf));
    Add(lib, "SINH", 1, 1, "Math", "Hyperbolic sine", unary([](double x) { return std::sinh(x); }, -inf, inf));
    Add(lib, "COSH", 1, 1, "Math", "Hyperbolic cosine", unary([](double x) { return std::cosh(x); }, -inf, inf));
    Add(lib, "TANH", 1, 1, "Math", "Hyperbolic tangent", unary([](double x) { return std::tanh(x); }, -inf, inf));
    Add(lib, "DEGREES", 1, 1, "Math", "Radians to degrees",
        unary([](double x) { return x * 180.0 / 3.14159265358979323846; }, -inf, inf));
    Add(lib, "RADIANS", 1, 1, "Math", "Degrees to radians",
        unary([](double x) { return x * 3.14159265358979323846 / 180.0; }, -inf, inf));
    Add(lib, "RANDBETWEEN", 2, 2, "Math", "A random integer between two numbers",
        [](const CallArgs& a) -> FormulaValue {
            const double lo = std::ceil(a.Number(0)), hi = std::floor(a.Number(1));
            if (!(lo <= hi) || std::fabs(lo) > 9.0e15 || std::fabs(hi) > 9.0e15)
                return Err(CellErrorType::NumError);
            static std::mt19937 gen(std::random_device{}());
            std::uniform_int_distribution<long long> dis(static_cast<long long>(lo), static_cast<long long>(hi));
            return Num(static_cast<double>(dis(gen)));
        }, true);
}

void RegisterStatistics(FormulaFunctionLibrary& lib) {
    auto spread = [](bool sample, bool root) {
        return [sample, root](const CallArgs& a) -> FormulaValue {
            std::vector<double> v;
            FormulaValue error;
            if (!CollectNumbers(a, 0, v, error)) return error;
            if (v.size() < (sample ? 2u : 1u)) return Err(CellErrorType::DivisionByZero);
            const double var = Variance(v, sample);
            return Num(root ? std::sqrt(var) : var);
        };
    };
    Add(lib, "STDEV", 1, 255, "Statistical", "Standard deviation of a sample", spread(true, true));
    Add(lib, "STDEV.S", 1, 255, "Statistical", "Standard deviation of a sample", spread(true, true));
    Add(lib, "STDEVP", 1, 255, "Statistical", "Standard deviation of a population", spread(false, true));
    Add(lib, "STDEV.P", 1, 255, "Statistical", "Standard deviation of a population", spread(false, true));
    Add(lib, "VAR", 1, 255, "Statistical", "Variance of a sample", spread(true, false));
    Add(lib, "VAR.S", 1, 255, "Statistical", "Variance of a sample", spread(true, false));
    Add(lib, "VARP", 1, 255, "Statistical", "Variance of a population", spread(false, false));
    Add(lib, "VAR.P", 1, 255, "Statistical", "Variance of a population", spread(false, false));

    auto kth = [](bool largest) {
        return [largest](const CallArgs& a) -> FormulaValue {
            std::vector<double> v;
            for (const FormulaValue& x : a.Flat(0)) {
                if (x.IsError()) return x;
                if (x.IsNumber()) v.push_back(x.GetNumber());
            }
            const double k = std::ceil(a.Number(1));
            if (k < 1 || k > static_cast<double>(v.size())) return Err(CellErrorType::NumError);
            std::sort(v.begin(), v.end());
            const size_t i = static_cast<size_t>(k) - 1;
            return Num(largest ? v[v.size() - 1 - i] : v[i]);
        };
    };
    Add(lib, "LARGE", 2, 2, "Statistical", "The k-th largest value", kth(true));
    Add(lib, "SMALL", 2, 2, "Statistical", "The k-th smallest value", kth(false));

    auto rank = [](const CallArgs& a) -> FormulaValue {
        const FormulaValue n = a.Value(0);
        if (n.IsError()) return n;
        const bool ascending = a.Count() > 2 && a.Number(2) != 0.0;
        const double x = n.ToNumber();
        bool present = false;
        int position = 1;
        for (const FormulaValue& v : a.Flat(1)) {
            if (!v.IsNumber()) continue;
            const double y = v.GetNumber();
            if (y == x) present = true;
            if (ascending ? y < x : y > x) ++position;
        }
        return present ? Num(position) : Err(CellErrorType::NAError);
    };
    Add(lib, "RANK", 2, 3, "Statistical", "Rank of a number in a list", rank);
    Add(lib, "RANK.EQ", 2, 3, "Statistical", "Rank of a number in a list", rank);

    auto mode = [](const CallArgs& a) -> FormulaValue {
        std::vector<double> v;
        FormulaValue error;
        if (!CollectNumbers(a, 0, v, error)) return error;
        // The most frequent; on a tie, the one that came first.
        std::map<double, int> counts;
        for (double x : v) ++counts[x];
        double best = 0.0;
        int bestCount = 1;
        for (double x : v) {
            if (counts[x] > bestCount) { bestCount = counts[x]; best = x; }
        }
        return bestCount > 1 ? Num(best) : Err(CellErrorType::NAError);
    };
    Add(lib, "MODE", 1, 255, "Statistical", "Most frequent value", mode);
    Add(lib, "MODE.SNGL", 1, 255, "Statistical", "Most frequent value", mode);

    Add(lib, "PERCENTILE", 2, 2, "Statistical", "The k-th percentile (inclusive)",
        [](const CallArgs& a) -> FormulaValue {
            std::vector<double> v;
            for (const FormulaValue& x : a.Flat(0)) {
                if (x.IsError()) return x;
                if (x.IsNumber()) v.push_back(x.GetNumber());
            }
            const double k = a.Number(1);
            if (v.empty() || k < 0 || k > 1) return Err(CellErrorType::NumError);
            return Num(Percentile(v, k));
        });
    Add(lib, "QUARTILE", 2, 2, "Statistical", "A quartile (inclusive)",
        [](const CallArgs& a) -> FormulaValue {
            std::vector<double> v;
            for (const FormulaValue& x : a.Flat(0)) {
                if (x.IsError()) return x;
                if (x.IsNumber()) v.push_back(x.GetNumber());
            }
            const double q = std::trunc(a.Number(1));
            if (v.empty() || q < 0 || q > 4) return Err(CellErrorType::NumError);
            return Num(Percentile(v, q / 4.0));
        });
}

void RegisterText(FormulaFunctionLibrary& lib) {
    // Character-wise, so Thai or accented text measures and cuts by letters.
    Add(lib, "LEN", 1, 1, "Text", "Number of characters", [](const CallArgs& a) -> FormulaValue {
        const FormulaValue v = a.Value(0);
        if (v.IsError()) return v;
        return Num(static_cast<double>(CharCount(v.GetText())));
    });
    auto side = [](bool left) {
        return [left](const CallArgs& a) -> FormulaValue {
            FormulaValue error;
            if (a.ScalarError(0, error)) return error;
            const std::string text = a.Value(0).GetText();
            const double n = a.Count() > 1 ? a.Number(1) : 1.0;
            if (n < 0) return Err(CellErrorType::ValueError);
            const size_t total = CharCount(text);
            const size_t count = std::min(total, static_cast<size_t>(std::min(n, 1e9)));
            return FormulaValue::Text(left ? CharSlice(text, 0, count) : CharSlice(text, total - count, count));
        };
    };
    Add(lib, "LEFT", 1, 2, "Text", "The first characters", side(true));
    Add(lib, "RIGHT", 1, 2, "Text", "The last characters", side(false));
    Add(lib, "MID", 3, 3, "Text", "Characters from the middle", [](const CallArgs& a) -> FormulaValue {
        FormulaValue error;
        if (a.ScalarError(0, error)) return error;
        const double start = a.Number(1), count = a.Number(2);
        if (start < 1 || count < 0) return Err(CellErrorType::ValueError);
        return FormulaValue::Text(CharSlice(a.Value(0).GetText(),
                                            static_cast<size_t>(std::min(start, 1e9)) - 1,
                                            static_cast<size_t>(std::min(count, 1e9))));
    });

    Add(lib, "CONCAT", 1, 255, "Text", "Join texts and ranges", [](const CallArgs& a) -> FormulaValue {
        std::string out;
        for (size_t i = 0; i < a.Count(); ++i) {
            for (const FormulaValue& v : a.Flat(i)) {
                if (v.IsError()) return v;
                out += v.GetText();
            }
        }
        return FormulaValue::Text(out);
    });
    Add(lib, "TEXTJOIN", 3, 255, "Text", "Join texts with a delimiter", [](const CallArgs& a) -> FormulaValue {
        FormulaValue error;
        if (a.ScalarError(0, error)) return error;
        const std::string delimiter = a.Value(0).GetText();
        const bool ignoreEmpty = a.Value(1).GetBoolean();
        std::string out;
        bool first = true;
        for (size_t i = 2; i < a.Count(); ++i) {
            for (const FormulaValue& v : a.Flat(i)) {
                if (v.IsError()) return v;
                const std::string t = v.GetText();
                if (ignoreEmpty && t.empty()) continue;
                if (!first) out += delimiter;
                out += t;
                first = false;
            }
        }
        return FormulaValue::Text(out);
    });
    Add(lib, "SUBSTITUTE", 3, 4, "Text", "Replace occurrences of a text", [](const CallArgs& a) -> FormulaValue {
        FormulaValue error;
        if (a.ScalarError(0, error)) return error;
        std::string text = a.Value(0).GetText();
        const std::string from = a.Value(1).GetText(), to = a.Value(2).GetText();
        if (from.empty()) return FormulaValue::Text(text);
        const int instance = a.Count() > 3 ? a.Int(3) : 0;
        if (a.Count() > 3 && instance < 1) return Err(CellErrorType::ValueError);
        std::string out;
        size_t pos = 0;
        int seen = 0;
        while (true) {
            const size_t hit = text.find(from, pos);
            if (hit == std::string::npos) break;
            ++seen;
            out.append(text, pos, hit - pos);
            out += (instance == 0 || seen == instance) ? to : from;
            pos = hit + from.size();
        }
        out.append(text, pos, std::string::npos);
        return FormulaValue::Text(out);
    });
    Add(lib, "REPLACE", 4, 4, "Text", "Replace characters at a position", [](const CallArgs& a) -> FormulaValue {
        FormulaValue error;
        if (a.ScalarError(0, error)) return error;
        const std::string text = a.Value(0).GetText();
        const double start = a.Number(1), count = a.Number(2);
        if (start < 1 || count < 0) return Err(CellErrorType::ValueError);
        const size_t total = CharCount(text);
        const size_t s = std::min(total, static_cast<size_t>(std::min(start, 1e9)) - 1);
        const size_t n = std::min(total - s, static_cast<size_t>(std::min(count, 1e9)));
        return FormulaValue::Text(CharSlice(text, 0, s) + a.Value(3).GetText() +
                                  CharSlice(text, s + n, total));
    });
    auto find = [](bool caseSensitive) {
        return [caseSensitive](const CallArgs& a) -> FormulaValue {
            FormulaValue error;
            if (a.ScalarError(0, error)) return error;
            const std::string needle = a.Value(0).GetText();
            const std::string hay = a.Value(1).GetText();
            const double start = a.Count() > 2 ? a.Number(2) : 1.0;
            const std::vector<size_t> offsets = CharOffsets(hay);
            const size_t chars = offsets.size() - 1;
            if (start < 1 || start > static_cast<double>(chars) + 1) return Err(CellErrorType::ValueError);
            for (size_t c = static_cast<size_t>(start) - 1; c <= chars; ++c) {
                const std::string rest = hay.substr(offsets[c]);
                bool hit;
                if (caseSensitive) {
                    hit = rest.compare(0, needle.size(), needle) == 0;
                } else if (HasWildcard(needle)) {
                    // SEARCH: the pattern may match any prefix of the rest.
                    hit = false;
                    const std::vector<size_t> ro = CharOffsets(rest);
                    for (size_t len = 0; len < ro.size() && !hit; ++len) {
                        hit = WildcardMatch(needle, rest.substr(0, ro[len]));
                    }
                } else {
                    hit = Lower(rest).compare(0, needle.size(), Lower(needle)) == 0;
                }
                if (hit) return Num(static_cast<double>(c + 1));
            }
            return Err(CellErrorType::ValueError);
        };
    };
    Add(lib, "FIND", 2, 3, "Text", "Position of a text (case-sensitive)", find(true));
    Add(lib, "SEARCH", 2, 3, "Text", "Position of a text (any case, wildcards)", find(false));
    Add(lib, "EXACT", 2, 2, "Text", "Whether two texts are the same, case included",
        [](const CallArgs& a) -> FormulaValue {
            return FormulaValue::Boolean(a.Value(0).GetText() == a.Value(1).GetText());
        });
    Add(lib, "PROPER", 1, 1, "Text", "Capitalize each word", [](const CallArgs& a) -> FormulaValue {
        std::string t = a.Value(0).GetText();
        bool start = true;
        for (char& c : t) {
            const bool letter = std::isalpha(static_cast<unsigned char>(c)) != 0;
            if (letter) c = static_cast<char>(start ? std::toupper(static_cast<unsigned char>(c))
                                                    : std::tolower(static_cast<unsigned char>(c)));
            start = !letter && static_cast<unsigned char>(c) < 0x80;
        }
        return FormulaValue::Text(t);
    });
    Add(lib, "REPT", 2, 2, "Text", "Repeat a text", [](const CallArgs& a) -> FormulaValue {
        const std::string t = a.Value(0).GetText();
        const double n = std::trunc(a.Number(1));
        // Excel's cell limit is 32,767 characters.
        if (n < 0 || n > 32767.0 || n * static_cast<double>(t.size()) > 32767.0 * 4)
            return Err(CellErrorType::ValueError);
        std::string out;
        for (int i = 0; i < static_cast<int>(n); ++i) out += t;
        return FormulaValue::Text(out);
    });
    Add(lib, "CHAR", 1, 1, "Text", "The character of a code", [](const CallArgs& a) -> FormulaValue {
        const double n = std::trunc(a.Number(0));
        if (n < 1 || n > 255) return Err(CellErrorType::ValueError);
        // Windows-1252, as Excel on Windows.
        static const uint16_t high[32] = {
            0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
            0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
            0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};
        const int code = static_cast<int>(n);
        std::string out;
        AppendUtf8(out, code >= 0x80 && code < 0xA0 ? high[code - 0x80] : static_cast<uint32_t>(code));
        return FormulaValue::Text(out);
    });
    Add(lib, "CODE", 1, 1, "Text", "The code of the first character", [](const CallArgs& a) -> FormulaValue {
        const std::string t = a.Value(0).GetText();
        if (t.empty()) return Err(CellErrorType::ValueError);
        return Num(static_cast<double>(FirstCodePoint(t)));
    });
    Add(lib, "CLEAN", 1, 1, "Text", "Remove control characters", [](const CallArgs& a) -> FormulaValue {
        std::string out;
        for (char c : a.Value(0).GetText()) {
            if (static_cast<unsigned char>(c) >= 32) out.push_back(c);
        }
        return FormulaValue::Text(out);
    });
    Add(lib, "VALUE", 1, 1, "Text", "A text as a number", [](const CallArgs& a) -> FormulaValue {
        const FormulaValue v = a.Value(0);
        if (v.IsError() || v.IsNumber()) return v;
        std::string t = v.GetText();
        while (!t.empty() && t.back() == ' ') t.pop_back();
        size_t lead = 0;
        while (lead < t.size() && t[lead] == ' ') ++lead;
        t = t.substr(lead);
        bool percent = !t.empty() && t.back() == '%';
        if (percent) t.pop_back();
        double n = 0.0;
        const char* f = t.c_str();
        if (!t.empty() && *f == '+') ++f;
        if (t.empty() || ParseFloatClassic(f, t.c_str() + t.size(), n) != t.c_str() + t.size())
            return Err(CellErrorType::ValueError);
        return Num(percent ? n / 100.0 : n);
    });
    Add(lib, "T", 1, 1, "Text", "The text, or empty", [](const CallArgs& a) -> FormulaValue {
        const FormulaValue v = a.Value(0);
        if (v.IsError()) return v;
        return FormulaValue::Text(v.IsText() ? v.GetText() : std::string());
    });
    Add(lib, "N", 1, 1, "Information", "A value as a number", [](const CallArgs& a) -> FormulaValue {
        const FormulaValue v = a.Value(0);
        if (v.IsError()) return v;
        if (v.IsNumber()) return v;
        if (v.IsBoolean()) return Num(v.GetBoolean() ? 1 : 0);
        return Num(0);
    });
}

void RegisterDates(FormulaFunctionLibrary& lib) {
    Add(lib, "TIME", 3, 3, "DateTime", "A time of day", [](const CallArgs& a) -> FormulaValue {
        FormulaValue error;
        if (a.ScalarError(0, error)) return error;
        const double seconds = std::trunc(a.Number(0)) * 3600.0 + std::trunc(a.Number(1)) * 60.0 +
                               std::trunc(a.Number(2));
        if (seconds < 0) return Err(CellErrorType::NumError);
        return Num(std::fmod(seconds, 86400.0) / 86400.0);
    });
    auto clockPart = [](int part) {   // 0 hour, 1 minute, 2 second
        return [part](const CallArgs& a) -> FormulaValue {
            const FormulaValue v = a.Value(0);
            if (v.IsError()) return v;
            const double s = v.ToNumber();
            if (s < 0) return Err(CellErrorType::NumError);
            long long secs = std::llround((s - std::floor(s)) * 86400.0) % 86400;
            const long long values[] = {secs / 3600, (secs / 60) % 60, secs % 60};
            return Num(static_cast<double>(values[part]));
        };
    };
    Add(lib, "HOUR", 1, 1, "DateTime", "Hour of a time", clockPart(0));
    Add(lib, "MINUTE", 1, 1, "DateTime", "Minute of a time", clockPart(1));
    Add(lib, "SECOND", 1, 1, "DateTime", "Second of a time", clockPart(2));
    Add(lib, "WEEKDAY", 1, 2, "DateTime", "Day of the week", [](const CallArgs& a) -> FormulaValue {
        const FormulaValue v = a.Value(0);
        if (v.IsError()) return v;
        const double s = std::floor(v.ToNumber());
        if (!ValidSerial(s)) return Err(CellErrorType::NumError);
        const long long day = static_cast<long long>(s);
        const int type = a.Count() > 1 ? a.Int(1) : 1;
        switch (type) {
            case 1: return Num(static_cast<double>((day + 6) % 7 + 1));   // Sunday = 1
            case 2: return Num(static_cast<double>((day + 5) % 7 + 1));   // Monday = 1
            case 3: return Num(static_cast<double>((day + 5) % 7));       // Monday = 0
            default: return Err(CellErrorType::NumError);
        }
    });
    auto months = [](bool endOfMonth) {
        return [endOfMonth](const CallArgs& a) -> FormulaValue {
            FormulaValue error;
            if (a.ScalarError(0, error)) return error;
            double out = 0.0;
            if (!AddMonths(a.Number(0), a.Int(1), endOfMonth, out))
                return Err(CellErrorType::NumError);
            return Num(out);
        };
    };
    Add(lib, "EDATE", 2, 2, "DateTime", "The date some months away", months(false));
    Add(lib, "EOMONTH", 2, 2, "DateTime", "The last day of a month some months away", months(true));
    Add(lib, "DAYS", 2, 2, "DateTime", "Days between two dates", [](const CallArgs& a) -> FormulaValue {
        FormulaValue error;
        if (a.ScalarError(0, error)) return error;
        return Num(std::floor(a.Number(0)) - std::floor(a.Number(1)));
    });
    Add(lib, "DATEDIF", 3, 3, "DateTime", "Years, months or days between two dates",
        [](const CallArgs& a) -> FormulaValue {
            FormulaValue error;
            if (a.ScalarError(0, error)) return error;
            const double s = std::floor(a.Number(0)), e = std::floor(a.Number(1));
            if (!ValidSerial(s) || !ValidSerial(e) || s > e) return Err(CellErrorType::NumError);
            std::string unit = a.Value(2).GetText();
            for (char& c : unit) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            int y1, m1, d1, y2, m2, d2;
            SerialToDate(s, y1, m1, d1);
            SerialToDate(e, y2, m2, d2);
            int monthsBetween = (y2 - y1) * 12 + (m2 - m1) - (d2 < d1 ? 1 : 0);
            if (unit == "D") return Num(e - s);
            if (unit == "M") return Num(monthsBetween);
            if (unit == "Y") return Num(monthsBetween / 12);
            if (unit == "YM") return Num(monthsBetween % 12);
            if (unit == "MD") {
                if (d2 >= d1) return Num(d2 - d1);
                const int pm = m2 == 1 ? 12 : m2 - 1, py = m2 == 1 ? y2 - 1 : y2;
                return Num(DaysInMonth(py, pm) - d1 + d2);
            }
            if (unit == "YD") {
                int yy = y2;
                if (m2 < m1 || (m2 == m1 && d2 < d1)) --yy;
                const double anniversary = DateToSerial(yy, m1, std::min(d1, DaysInMonth(yy, m1)));
                return Num(e - anniversary);
            }
            return Err(CellErrorType::NumError);
        });
    Add(lib, "DATEVALUE", 1, 1, "DateTime", "A date written as text (YYYY-MM-DD)",
        [](const CallArgs& a) -> FormulaValue {
            const FormulaValue v = a.Value(0);
            if (v.IsError()) return v;
            const std::string t = v.GetText();
            int y = 0, m = 0, d = 0;
            char s1 = 0, s2 = 0;
            if (std::sscanf(t.c_str(), "%d%c%d%c%d", &y, &s1, &m, &s2, &d) != 5 || s1 != s2 ||
                (s1 != '-' && s1 != '/') || y < 1900 || y > 9999 || m < 1 || m > 12 || d < 1 ||
                d > DaysInMonth(y, m))
                return Err(CellErrorType::ValueError);
            return Num(DateToSerial(y, m, d));
        });
}

void RegisterLogicAndInformation(FormulaFunctionLibrary& lib) {
    Add(lib, "IFNA", 2, 2, "Logical", "A value, or another if it is #N/A", [](const CallArgs& a) -> FormulaValue {
        const FormulaValue v = a.Value(0);
        return v.IsError() && v.GetError() == CellErrorType::NAError ? a.Value(1) : v;
    });
    Add(lib, "IFS", 2, 254, "Logical", "The value of the first true condition", [](const CallArgs& a) -> FormulaValue {
        if (a.Count() % 2 != 0) return Err(CellErrorType::ValueError);
        for (size_t i = 0; i + 1 < a.Count(); i += 2) {
            const FormulaValue c = a.Value(i);
            if (c.IsError()) return c;
            if (c.GetBoolean()) return a.Value(i + 1);
        }
        return Err(CellErrorType::NAError);
    });
    Add(lib, "SWITCH", 3, 254, "Logical", "The result matching a value", [](const CallArgs& a) -> FormulaValue {
        const FormulaValue v = a.Value(0);
        if (v.IsError()) return v;
        size_t i = 1;
        for (; i + 1 < a.Count(); i += 2) {
            if (SameValue(v, a.Value(i), false)) return a.Value(i + 1);
        }
        return i < a.Count() ? a.Value(i) : Err(CellErrorType::NAError);
    });
    Add(lib, "XOR", 1, 255, "Logical", "True when an odd number of values are true",
        [](const CallArgs& a) -> FormulaValue {
            int trues = 0;
            for (size_t i = 0; i < a.Count(); ++i) {
                for (const FormulaValue& v : a.Flat(i)) {
                    if (v.IsError()) return v;
                    if ((v.IsBoolean() || v.IsNumber()) && v.GetBoolean()) ++trues;
                }
            }
            return FormulaValue::Boolean(trues % 2 == 1);
        });
    Add(lib, "NA", 0, 0, "Information", "The #N/A error",
        [](const CallArgs&) -> FormulaValue { return Err(CellErrorType::NAError); });
    auto is = [](std::function<bool(const FormulaValue&)> test) {
        return [test](const CallArgs& a) -> FormulaValue { return FormulaValue::Boolean(test(a.Value(0))); };
    };
    Add(lib, "ISNA", 1, 1, "Information", "Whether a value is #N/A", is([](const FormulaValue& v) {
        return v.IsError() && v.GetError() == CellErrorType::NAError;
    }));
    Add(lib, "ISERR", 1, 1, "Information", "Whether a value is an error other than #N/A",
        is([](const FormulaValue& v) { return v.IsError() && v.GetError() != CellErrorType::NAError; }));
    Add(lib, "ISLOGICAL", 1, 1, "Information", "Whether a value is TRUE or FALSE",
        is([](const FormulaValue& v) { return v.IsBoolean(); }));
    Add(lib, "ISNONTEXT", 1, 1, "Information", "Whether a value is not text",
        is([](const FormulaValue& v) { return !v.IsText(); }));
    auto parity = [](bool odd) {
        return [odd](const CallArgs& a) -> FormulaValue {
            const FormulaValue v = a.Value(0);
            if (v.IsError()) return v;
            const double n = std::trunc(std::fabs(v.ToNumber()));
            return FormulaValue::Boolean((std::fmod(n, 2.0) != 0.0) == odd);
        };
    };
    Add(lib, "ISEVEN", 1, 1, "Information", "Whether a number is even", parity(false));
    Add(lib, "ISODD", 1, 1, "Information", "Whether a number is odd", parity(true));
}

void RegisterFinance(FormulaFunctionLibrary& lib) {
    // FV(rate, nper, pmt, [pv], [type]) and PV(rate, nper, pmt, [fv], [type]).
    Add(lib, "FV", 3, 5, "Financial", "Future value of an investment", [](const CallArgs& a) -> FormulaValue {
        FormulaValue error;
        if (a.ScalarError(0, error)) return error;
        const double r = a.Number(0), n = a.Number(1), pmt = a.Number(2);
        const double pv = a.Number(3), type = a.Number(4) != 0 ? 1 : 0;
        if (r == 0) return Num(-(pv + pmt * n));
        const double g = std::pow(1 + r, n);
        return Num(-(pv * g + pmt * (1 + r * type) * (g - 1) / r));
    });
    Add(lib, "PV", 3, 5, "Financial", "Present value of an investment", [](const CallArgs& a) -> FormulaValue {
        FormulaValue error;
        if (a.ScalarError(0, error)) return error;
        const double r = a.Number(0), n = a.Number(1), pmt = a.Number(2);
        const double fv = a.Number(3), type = a.Number(4) != 0 ? 1 : 0;
        if (r == 0) return Num(-(fv + pmt * n));
        const double g = std::pow(1 + r, n);
        return Num(-(fv + pmt * (1 + r * type) * (g - 1) / r) / g);
    });
    Add(lib, "NPV", 2, 255, "Financial", "Net present value of cash flows", [](const CallArgs& a) -> FormulaValue {
        FormulaValue error;
        if (!a.IsRange(0) && a.Value(0).IsError()) return a.Value(0);
        const double r = a.Number(0);
        std::vector<double> flows;
        if (!CollectNumbers(a, 1, flows, error)) return error;
        double npv = 0.0;
        for (size_t i = 0; i < flows.size(); ++i) npv += flows[i] / std::pow(1 + r, static_cast<double>(i + 1));
        return Num(npv);
    });
}

// ===== DYNAMIC ARRAYS =====

// Rows (or, by column, columns) of a grid as lines of values.
std::vector<Values> Lines(const Grid& g, bool byColumn) {
    std::vector<Values> lines;
    const int n = byColumn ? g.cols : g.rows;
    for (int i = 0; i < n; ++i) lines.push_back(byColumn ? Column(g, i) : Row(g, i));
    return lines;
}

// Lines back into an array: rows, or columns side by side.
FormulaValue FromLines(const std::vector<Values>& lines, bool byColumn) {
    if (lines.empty() || lines.front().empty()) return Err(CellErrorType::Calc);
    const int count = static_cast<int>(lines.size());
    const int length = static_cast<int>(lines.front().size());
    Values cells;
    if (!byColumn) {
        for (const Values& line : lines) cells.insert(cells.end(), line.begin(), line.end());
        return FormulaValue::Array(count, length, std::move(cells));
    }
    for (int r = 0; r < length; ++r) {
        for (int c = 0; c < count; ++c) cells.push_back(lines[static_cast<size_t>(c)][static_cast<size_t>(r)]);
    }
    return FormulaValue::Array(length, count, std::move(cells));
}

// The order SORT and SORTBY put values in: numbers, text (without regard to
// case), logical values, then errors; an empty cell counts as 0.
int SortCompare(const FormulaValue& a, const FormulaValue& b) {
    if (a.IsError() || b.IsError()) return static_cast<int>(a.IsError()) - static_cast<int>(b.IsError());
    return CompareValues(a, b);
}

// A value as a condition: TRUE / a non-zero number keeps; text is #VALUE!.
bool Truth(const FormulaValue& v, FormulaValue& error) {
    if (v.IsError()) { error = v; return false; }
    if (v.IsText()) { error = Err(CellErrorType::ValueError); return false; }
    return v.ToNumber() != 0.0;
}

// Two values the same for UNIQUE: text without regard to case.
bool SameForUnique(const FormulaValue& a, const FormulaValue& b) {
    if (a.IsEmpty() && b.IsEmpty()) return true;
    if (a.IsError() && b.IsError()) return a.GetError() == b.GetError();
    return SameValue(a, b, false);
}

// A LAMBDA argument called with these values; #VALUE! when it is none.
FormulaValue Call(const CallArgs& a, size_t lambdaArg, const std::vector<FormulaValue>& values) {
    const FormulaValue fn = a.Value(lambdaArg);
    if (!fn.IsLambda() || !a.Evaluator()) return fn.IsError() ? fn : Err(CellErrorType::ValueError);
    return a.Evaluator()->CallLambda(fn, values);
}

constexpr double kMaxArrayCells = 1048576.0 * 16.0;   // more than any sheet shows

void RegisterDynamicArrays(FormulaFunctionLibrary& lib) {
    // FILTER(array, include, [if_empty])
    Add(lib, "FILTER", 2, 3, "Lookup", "The rows or columns of an array that meet a condition",
        [](const CallArgs& a) -> FormulaValue {
            const Grid g = a.GetGrid(0);
            const Grid include = a.GetGrid(1);
            if (g.Empty() || include.Empty()) return Err(CellErrorType::ValueError);
            if (g.cells.size() == 1 && g.cells.front().IsError()) return g.cells.front();
            bool byColumn;
            if (include.cols == 1 && include.rows == g.rows) byColumn = false;
            else if (include.rows == 1 && include.cols == g.cols) byColumn = true;
            else return Err(CellErrorType::ValueError);
            std::vector<Values> kept;
            const std::vector<Values> lines = Lines(g, byColumn);
            for (size_t i = 0; i < lines.size(); ++i) {
                FormulaValue error;
                const bool keep = Truth(include.cells[i], error);
                if (!error.IsEmpty()) return error;
                if (keep) kept.push_back(lines[i]);
            }
            if (kept.empty()) {
                if (a.Count() > 2 && !a.Omitted(2)) return GridValue(a.GetGrid(2));
                return Err(CellErrorType::Calc);
            }
            return FromLines(kept, byColumn);
        });

    // SORT(array, [sort_index], [sort_order], [by_col])
    Add(lib, "SORT", 1, 4, "Lookup", "An array sorted by one or more of its columns or rows",
        [](const CallArgs& a) -> FormulaValue {
            const Grid g = a.GetGrid(0);
            if (g.Empty()) return Err(CellErrorType::ValueError);
            const bool byColumn = !a.Omitted(3) && a.Value(3).ToNumber() != 0.0;
            std::vector<int> keys{1};
            std::vector<int> orders{1};
            if (!a.Omitted(1)) {
                keys.clear();
                for (const FormulaValue& v : a.GetGrid(1).cells) {
                    if (v.IsError()) return v;
                    keys.push_back(ToInt(v.ToNumber()));
                }
            }
            if (!a.Omitted(2)) {
                orders.clear();
                for (const FormulaValue& v : a.GetGrid(2).cells) {
                    if (v.IsError()) return v;
                    orders.push_back(ToInt(v.ToNumber()));
                }
            }
            const int width = byColumn ? g.rows : g.cols;
            for (int k : keys) if (k < 1 || k > width) return Err(CellErrorType::ValueError);
            for (int o : orders) if (o != 1 && o != -1) return Err(CellErrorType::ValueError);
            if (orders.size() != 1 && orders.size() != keys.size()) return Err(CellErrorType::ValueError);
            std::vector<Values> lines = Lines(g, byColumn);
            std::stable_sort(lines.begin(), lines.end(), [&](const Values& x, const Values& y) {
                for (size_t k = 0; k < keys.size(); ++k) {
                    const size_t at = static_cast<size_t>(keys[k] - 1);
                    const int order = orders.size() == 1 ? orders.front() : orders[k];
                    const int c = SortCompare(x[at], y[at]);
                    if (c != 0) return order > 0 ? c < 0 : c > 0;
                }
                return false;
            });
            return FromLines(lines, byColumn);
        });

    // SORTBY(array, by_array1, [sort_order1], [by_array2, [sort_order2]], ...)
    Add(lib, "SORTBY", 2, 255, "Lookup", "An array sorted by the values of other arrays",
        [](const CallArgs& a) -> FormulaValue {
            const Grid g = a.GetGrid(0);
            if (g.Empty()) return Err(CellErrorType::ValueError);
            std::vector<std::pair<Values, int>> keys;
            int direction = 0;   // 1 sorts rows, 2 sorts columns
            for (size_t i = 1; i < a.Count();) {
                const Grid by = a.GetGrid(i);
                int order = 1;
                if (i + 1 < a.Count() && !a.IsRange(i + 1)) {
                    order = a.Omitted(i + 1) ? 1 : a.Int(i + 1);
                    i += 2;
                } else {
                    i += 1;
                }
                if (order != 1 && order != -1) return Err(CellErrorType::ValueError);
                int dir = 0;
                if (by.cols == 1 && by.rows == g.rows) dir = 1;
                else if (by.rows == 1 && by.cols == g.cols) dir = 2;
                if (dir == 0 || (direction != 0 && dir != direction)) return Err(CellErrorType::ValueError);
                direction = dir;
                keys.emplace_back(by.cells, order);
            }
            const bool byColumn = direction == 2;
            const std::vector<Values> lines = Lines(g, byColumn);
            std::vector<size_t> order(lines.size());
            for (size_t i = 0; i < order.size(); ++i) order[i] = i;
            std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) {
                for (const auto& [values, dir] : keys) {
                    const int c = SortCompare(values[x], values[y]);
                    if (c != 0) return dir > 0 ? c < 0 : c > 0;
                }
                return false;
            });
            std::vector<Values> sorted;
            for (size_t i : order) sorted.push_back(lines[i]);
            return FromLines(sorted, byColumn);
        });

    // UNIQUE(array, [by_col], [exactly_once])
    Add(lib, "UNIQUE", 1, 3, "Lookup", "The distinct rows or columns of an array",
        [](const CallArgs& a) -> FormulaValue {
            const Grid g = a.GetGrid(0);
            if (g.Empty()) return Err(CellErrorType::ValueError);
            const bool byColumn = !a.Omitted(1) && a.Value(1).ToNumber() != 0.0;
            const bool exactlyOnce = !a.Omitted(2) && a.Value(2).ToNumber() != 0.0;
            const std::vector<Values> lines = Lines(g, byColumn);
            auto same = [](const Values& x, const Values& y) {
                for (size_t i = 0; i < x.size(); ++i) {
                    if (!SameForUnique(x[i], y[i])) return false;
                }
                return true;
            };
            std::vector<Values> distinct;
            std::vector<int> counts;
            for (const Values& line : lines) {
                size_t found = distinct.size();
                for (size_t i = 0; i < distinct.size(); ++i) {
                    if (same(distinct[i], line)) { found = i; break; }
                }
                if (found == distinct.size()) { distinct.push_back(line); counts.push_back(1); }
                else ++counts[found];
            }
            std::vector<Values> out;
            for (size_t i = 0; i < distinct.size(); ++i) {
                if (!exactlyOnce || counts[i] == 1) out.push_back(distinct[i]);
            }
            if (out.empty()) return Err(CellErrorType::Calc);
            return FromLines(out, byColumn);
        });

    // SEQUENCE(rows, [columns], [start], [step])
    Add(lib, "SEQUENCE", 1, 4, "Math", "A sequence of numbers in rows and columns",
        [](const CallArgs& a) -> FormulaValue {
            FormulaValue error;
            if (a.ScalarError(0, error)) return error;
            const double rows = std::trunc(a.Number(0));
            const double cols = a.Omitted(1) ? 1.0 : std::trunc(a.Number(1));
            const double start = a.Omitted(2) ? 1.0 : a.Number(2);
            const double step = a.Omitted(3) ? 1.0 : a.Number(3);
            if (rows < 0 || cols < 0) return Err(CellErrorType::ValueError);
            if (rows == 0 || cols == 0) return Err(CellErrorType::Calc);
            if (rows * cols > kMaxArrayCells) return Err(CellErrorType::NumError);
            Values cells;
            const long long total = static_cast<long long>(rows * cols);
            cells.reserve(static_cast<size_t>(total));
            for (long long i = 0; i < total; ++i) cells.push_back(Num(start + step * static_cast<double>(i)));
            return FormulaValue::Array(static_cast<int>(rows), static_cast<int>(cols), std::move(cells));
        });

    // RANDARRAY([rows], [columns], [min], [max], [whole_number])
    Add(lib, "RANDARRAY", 0, 5, "Math", "An array of random numbers",
        [](const CallArgs& a) -> FormulaValue {
            FormulaValue error;
            if (a.ScalarError(0, error)) return error;
            const double rows = a.Omitted(0) ? 1.0 : std::trunc(a.Number(0));
            const double cols = a.Omitted(1) ? 1.0 : std::trunc(a.Number(1));
            const double low = a.Omitted(2) ? 0.0 : a.Number(2);
            const double high = a.Omitted(3) ? 1.0 : a.Number(3);
            const bool whole = !a.Omitted(4) && a.Value(4).ToNumber() != 0.0;
            if (rows < 0 || cols < 0 || low > high) return Err(CellErrorType::ValueError);
            if (rows == 0 || cols == 0) return Err(CellErrorType::Calc);
            if (rows * cols > kMaxArrayCells) return Err(CellErrorType::NumError);
            if (whole && (low != std::trunc(low) || high != std::trunc(high))) return Err(CellErrorType::ValueError);
            thread_local std::mt19937_64 engine{std::random_device{}()};
            Values cells;
            const long long total = static_cast<long long>(rows * cols);
            for (long long i = 0; i < total; ++i) {
                if (whole) {
                    std::uniform_int_distribution<long long> d(static_cast<long long>(low), static_cast<long long>(high));
                    cells.push_back(Num(static_cast<double>(d(engine))));
                } else {
                    std::uniform_real_distribution<double> d(low, high);
                    cells.push_back(Num(d(engine)));
                }
            }
            return FormulaValue::Array(static_cast<int>(rows), static_cast<int>(cols), std::move(cells));
        }, true);

    // TRANSPOSE(array)
    Add(lib, "TRANSPOSE", 1, 1, "Lookup", "An array with its rows and columns swapped",
        [](const CallArgs& a) -> FormulaValue {
            const Grid g = a.GetGrid(0);
            if (g.Empty()) return Err(CellErrorType::ValueError);
            Values cells;
            for (int c = 0; c < g.cols; ++c) {
                for (int r = 0; r < g.rows; ++r) cells.push_back(g.At(r, c));
            }
            return FormulaValue::Array(g.cols, g.rows, std::move(cells));
        });

    // LET and LAMBDA choose themselves which arguments to evaluate, so the
    // evaluator runs them (FormulaEvaluator::EvaluateLet / EvaluateLambda);
    // they are listed here for the function menu, the hints and the readers.
    Add(lib, "LET", 3, 253, "Logical", "Names values for use in a calculation",
        [](const CallArgs&) -> FormulaValue { return Err(CellErrorType::ValueError); });
    Add(lib, "LAMBDA", 1, 254, "Logical", "A calculation with parameters, called like a function",
        [](const CallArgs&) -> FormulaValue { return Err(CellErrorType::Calc); });

    // ===== LAMBDA HELPERS =====

    // MAP(array1, [array2, ...], lambda): the lambda on each element.
    Add(lib, "MAP", 2, 255, "Logical", "A LAMBDA applied to each value of one or more arrays",
        [](const CallArgs& a) -> FormulaValue {
            const size_t fn = a.Count() - 1;
            std::vector<Grid> grids;
            for (size_t i = 0; i < fn; ++i) grids.push_back(a.GetGrid(i));
            for (const Grid& g : grids) {
                if (g.rows != grids.front().rows || g.cols != grids.front().cols) return Err(CellErrorType::ValueError);
            }
            const Grid& shape = grids.front();
            Values cells;
            for (size_t k = 0; k < shape.cells.size(); ++k) {
                std::vector<FormulaValue> values;
                for (const Grid& g : grids) values.push_back(g.cells[k]);
                const FormulaValue r = Call(a, fn, values);
                cells.push_back(r.IsArray() ? Err(CellErrorType::Calc) : r);
            }
            return FormulaValue::Array(shape.rows, shape.cols, std::move(cells));
        });

    // BYROW(array, lambda) / BYCOL(array, lambda): one value per row / column.
    auto byLine = [](bool byColumn) {
        return [byColumn](const CallArgs& a) -> FormulaValue {
            const Grid g = a.GetGrid(0);
            if (g.Empty()) return Err(CellErrorType::ValueError);
            Values out;
            for (const Values& line : Lines(g, byColumn)) {
                const int rows = byColumn ? static_cast<int>(line.size()) : 1;
                const int cols = byColumn ? 1 : static_cast<int>(line.size());
                const FormulaValue r = Call(a, 1, {FormulaValue::Array(rows, cols, line)});
                out.push_back(r.IsArray() ? Err(CellErrorType::Calc) : r);
            }
            const int n = static_cast<int>(out.size());
            return byColumn ? FormulaValue::Array(1, n, std::move(out)) : FormulaValue::Array(n, 1, std::move(out));
        };
    };
    Add(lib, "BYROW", 2, 2, "Logical", "A LAMBDA applied to each row of an array", byLine(false));
    Add(lib, "BYCOL", 2, 2, "Logical", "A LAMBDA applied to each column of an array", byLine(true));

    // REDUCE([initial_value], array, lambda) / SCAN(...): an accumulator
    // carried over the values; SCAN keeps every step.
    auto fold = [](bool keepSteps) {
        return [keepSteps](const CallArgs& a) -> FormulaValue {
            const bool hasInitial = a.Count() == 3;
            const size_t arrayArg = hasInitial ? 1 : 0;
            const Grid g = a.GetGrid(arrayArg);
            FormulaValue acc = hasInitial && !a.Omitted(0) ? a.Value(0) : FormulaValue::Empty();
            Values steps;
            for (const FormulaValue& v : g.cells) {
                acc = Call(a, arrayArg + 1, {acc, v});
                if (keepSteps) steps.push_back(acc.IsArray() ? Err(CellErrorType::Calc) : acc);
            }
            if (keepSteps) return FormulaValue::Array(g.rows, g.cols, std::move(steps));
            return acc;
        };
    };
    Add(lib, "REDUCE", 2, 3, "Logical", "A LAMBDA folding an array into one value", fold(false));
    Add(lib, "SCAN", 2, 3, "Logical", "A LAMBDA folding an array, every step kept", fold(true));

    // MAKEARRAY(rows, columns, lambda(row, column))
    Add(lib, "MAKEARRAY", 3, 3, "Logical", "An array whose values a LAMBDA gives",
        [](const CallArgs& a) -> FormulaValue {
            const double rows = std::trunc(a.Number(0)), cols = std::trunc(a.Number(1));
            if (rows < 1 || cols < 1) return Err(CellErrorType::ValueError);
            if (rows * cols > kMaxArrayCells) return Err(CellErrorType::NumError);
            Values cells;
            for (int r = 1; r <= static_cast<int>(rows); ++r) {
                for (int c = 1; c <= static_cast<int>(cols); ++c) {
                    const FormulaValue v = Call(a, 2, {Num(r), Num(c)});
                    cells.push_back(v.IsArray() ? Err(CellErrorType::Calc) : v);
                }
            }
            return FormulaValue::Array(static_cast<int>(rows), static_cast<int>(cols), std::move(cells));
        });
}

// ===== TIME VALUE OF MONEY AND DEPRECIATION =====
// The formulas and conventions are Excel's and LibreOffice's (sc/source/core/
// tool/interpr2.cxx): type 0 pays at the end of a period, 1 at its start;
// money paid out is negative.

double FutureValue(double r, double n, double pmt, double pv, bool atStart) {
    if (r == 0) return -(pv + pmt * n);
    const double g = std::pow(1 + r, n);
    return -(pv * g + pmt * (1 + r * (atStart ? 1 : 0)) * (g - 1) / r);
}

double Payment(double r, double n, double pv, double fv, bool atStart) {
    if (r == 0) return -(pv + fv) / n;
    const double g = std::pow(1 + r, n);
    return -(r * (pv * g + fv)) / ((1 + r * (atStart ? 1 : 0)) * (g - 1));
}

// The interest part of payment `per` (LibreOffice's ScGetIpmt).
double InterestPayment(double r, double per, double n, double pv, double fv, bool atStart) {
    const double pmt = Payment(r, n, pv, fv, atStart);
    double ipmt;
    if (per == 1.0) ipmt = atStart ? 0.0 : -pv;
    else if (atStart) ipmt = FutureValue(r, per - 2.0, pmt, pv, true) - pmt;
    else ipmt = FutureValue(r, per - 1.0, pmt, pv, false);
    return ipmt * r;
}

// The cash flows of IRR / MIRR / XIRR: numbers only from ranges.
bool CashFlows(const CallArgs& a, size_t i, std::vector<double>& out, FormulaValue& error) {
    for (const FormulaValue& v : a.Flat(i)) {
        if (v.IsError()) { error = v; return false; }
        if (v.IsNumber()) out.push_back(v.GetNumber());
        else if (!a.IsRange(i)) out.push_back(v.ToNumber());
    }
    return true;
}

bool HasBothSigns(const std::vector<double>& v) {
    bool positive = false, negative = false;
    for (double x : v) { positive = positive || x > 0; negative = negative || x < 0; }
    return positive && negative;
}

// Excel's DDB step (LibreOffice's ScGetGDA): the double-declining
// depreciation of one period.
double DecliningStep(double cost, double salvage, double life, double period, double factor) {
    double rate = factor / life;
    double oldValue;
    if (rate >= 1.0) {
        rate = 1.0;
        oldValue = period == 1.0 ? cost : 0.0;
    } else {
        oldValue = cost * std::pow(1.0 - rate, period - 1.0);
    }
    const double newValue = cost * std::pow(1.0 - rate, period);
    const double step = newValue < salvage ? oldValue - salvage : oldValue - newValue;
    return step < 0.0 ? 0.0 : step;
}

// VDB over whole periods with the switch to straight line (ScInterVDB).
double VdbWhole(double cost, double salvage, double life, double life1, double period, double factor) {
    double vdb = 0.0;
    const double intEnd = std::ceil(period);
    const long loopEnd = static_cast<long>(intEnd);
    double term = 0.0, sln = 0.0;
    double remaining = cost - salvage;
    bool straight = false;
    for (long i = 1; i <= loopEnd; ++i) {
        if (!straight) {
            const double ddb = DecliningStep(cost, salvage, life, static_cast<double>(i), factor);
            sln = remaining / (life1 - static_cast<double>(i - 1));
            if (sln > ddb) {
                term = sln;
                straight = true;
            } else {
                term = ddb;
                remaining -= ddb;
            }
        } else {
            term = sln;
        }
        if (i == loopEnd) term *= (period + 1.0 - intEnd);
        vdb += term;
    }
    return vdb;
}

void RegisterMoreFinance(FormulaFunctionLibrary& lib) {
    // NPER(rate, pmt, pv, [fv], [type])
    Add(lib, "NPER", 3, 5, "Financial", "Number of periods of an investment", [](const CallArgs& a) -> FormulaValue {
        FormulaValue error;
        if (a.ScalarError(0, error)) return error;
        const double r = a.Number(0), pmt = a.Number(1), pv = a.Number(2), fv = a.Number(3);
        const double type = a.Number(4) != 0 ? 1 : 0;
        if (r == 0) {
            if (pmt == 0) return Err(CellErrorType::NumError);
            return Num(-(pv + fv) / pmt);
        }
        const double num = pmt * (1 + r * type) - fv * r;
        const double den = pv * r + pmt * (1 + r * type);
        if (den == 0 || num / den <= 0 || r <= -1) return Err(CellErrorType::NumError);
        return Num(std::log(num / den) / std::log(1 + r));
    });

    // RATE(nper, pmt, pv, [fv], [type], [guess]): Newton's method from the
    // guess (10%), within 1E-7 in at most 20 steps, as Excel documents it;
    // #NUM! when it does not converge.
    Add(lib, "RATE", 3, 6, "Financial", "Interest rate per period of an annuity", [](const CallArgs& a) -> FormulaValue {
        FormulaValue error;
        if (a.ScalarError(0, error)) return error;
        const double n = a.Number(0), pmt = a.Number(1), pv = a.Number(2), fv = a.Number(3);
        const double t = a.Number(4) != 0 ? 1 : 0;
        double r = a.Omitted(5) ? 0.1 : a.Number(5);
        if (n <= 0) return Err(CellErrorType::NumError);
        auto f = [&](double x) {
            if (std::fabs(x) < 1e-12) return pv + pmt * n + fv;
            const double g = std::pow(1 + x, n);
            return pv * g + pmt * (1 + x * t) * (g - 1) / x + fv;
        };
        auto df = [&](double x) {
            if (std::fabs(x) < 1e-12) return pv * n + pmt * (t * n + n * (n - 1) / 2.0);
            const double g = std::pow(1 + x, n);
            const double dg = n * std::pow(1 + x, n - 1);
            return pv * dg + pmt * t * (g - 1) / x + pmt * (1 + x * t) * (dg * x - (g - 1)) / (x * x);
        };
        for (int i = 0; i < 20; ++i) {
            const double d = df(r);
            if (d == 0 || !std::isfinite(d)) return Err(CellErrorType::NumError);
            const double next = r - f(r) / d;
            if (!std::isfinite(next) || next <= -1) return Err(CellErrorType::NumError);
            if (std::fabs(next - r) < 1e-7) return Num(next);
            r = next;
        }
        return Err(CellErrorType::NumError);
    });

    // IPMT / PPMT(rate, per, nper, pv, [fv], [type])
    auto part = [](bool principal) {
        return [principal](const CallArgs& a) -> FormulaValue {
            FormulaValue error;
            if (a.ScalarError(0, error)) return error;
            const double r = a.Number(0), per = a.Number(1), n = a.Number(2), pv = a.Number(3);
            const double fv = a.Number(4);
            const bool atStart = a.Number(5) != 0;
            if (per < 1 || per > n || n <= 0) return Err(CellErrorType::NumError);
            const double ipmt = InterestPayment(r, per, n, pv, fv, atStart);
            return Num(principal ? Payment(r, n, pv, fv, atStart) - ipmt : ipmt);
        };
    };
    Add(lib, "IPMT", 4, 6, "Financial", "Interest part of one payment", part(false));
    Add(lib, "PPMT", 4, 6, "Financial", "Principal part of one payment", part(true));

    // CUMIPMT / CUMPRINC(rate, nper, pv, start_period, end_period, type)
    auto cumulative = [](bool principal) {
        return [principal](const CallArgs& a) -> FormulaValue {
            FormulaValue error;
            if (a.ScalarError(0, error)) return error;
            const double r = a.Number(0), n = a.Number(1), pv = a.Number(2);
            long start = static_cast<long>(std::trunc(a.Number(3)));
            const long end = static_cast<long>(std::trunc(a.Number(4)));
            const double type = a.Number(5);
            if (r <= 0 || n <= 0 || pv <= 0 || start < 1 || end < start || end > n ||
                (type != 0 && type != 1)) {
                return Err(CellErrorType::NumError);
            }
            const bool atStart = type == 1;
            const double pmt = Payment(r, n, pv, 0.0, atStart);
            double sum = 0.0;
            if (start == 1) {
                if (principal) sum = atStart ? pmt : pmt + pv * r;
                else if (!atStart) sum = -pv;
                ++start;
            }
            for (long i = start; i <= end; ++i) {
                const double d = static_cast<double>(i);
                const double interest = atStart ? FutureValue(r, d - 2, pmt, pv, true) - pmt
                                                : FutureValue(r, d - 1, pmt, pv, false);
                sum += principal ? pmt - interest * r : interest;
            }
            return Num(principal ? sum : sum * r);
        };
    };
    Add(lib, "CUMIPMT", 6, 6, "Financial", "Interest paid between two periods", cumulative(false));
    Add(lib, "CUMPRINC", 6, 6, "Financial", "Principal paid between two periods", cumulative(true));

    // IRR(values, [guess]): Newton's method, within 1E-7 in at most 20 steps
    // (Excel's and LibreOffice's limits); #NUM! otherwise.
    Add(lib, "IRR", 1, 2, "Financial", "Internal rate of return of cash flows", [](const CallArgs& a) -> FormulaValue {
        std::vector<double> flows;
        FormulaValue error;
        if (!CashFlows(a, 0, flows, error)) return error;
        if (!HasBothSigns(flows)) return Err(CellErrorType::NumError);
        double r = a.Omitted(1) ? 0.1 : a.Number(1);
        for (int i = 0; i < 20; ++i) {
            double value = 0.0, slope = 0.0;
            for (size_t k = 0; k < flows.size(); ++k) {
                const double d = std::pow(1 + r, static_cast<double>(k));
                value += flows[k] / d;
                slope -= static_cast<double>(k) * flows[k] / (d * (1 + r));
            }
            if (slope == 0 || !std::isfinite(slope)) return Err(CellErrorType::NumError);
            const double next = r - value / slope;
            if (!std::isfinite(next) || next <= -1) return Err(CellErrorType::NumError);
            if (std::fabs(next - r) < 1e-7) return Num(next);
            r = next;
        }
        return Err(CellErrorType::NumError);
    });

    // XNPV(rate, values, dates) and XIRR(values, dates, [guess]): years of
    // 365 days from the first date.
    auto datedFlows = [](const CallArgs& a, size_t valuesArg, size_t datesArg, std::vector<double>& flows,
                         std::vector<double>& days, FormulaValue& error) {
        const Values values = a.Flat(valuesArg), dates = a.Flat(datesArg);
        if (values.size() != dates.size() || values.empty()) { error = Err(CellErrorType::NumError); return false; }
        for (size_t k = 0; k < values.size(); ++k) {
            if (values[k].IsError()) { error = values[k]; return false; }
            if (dates[k].IsError()) { error = dates[k]; return false; }
            if (!values[k].IsNumber() && !values[k].IsBoolean()) { error = Err(CellErrorType::ValueError); return false; }
            const double d = std::trunc(dates[k].ToNumber());
            if (!ValidSerial(d)) { error = Err(CellErrorType::NumError); return false; }
            flows.push_back(values[k].ToNumber());
            days.push_back(d);
        }
        for (double d : days) {
            if (d < days.front()) { error = Err(CellErrorType::NumError); return false; }
        }
        return true;
    };
    Add(lib, "XNPV", 3, 3, "Financial", "Net present value of dated cash flows",
        [datedFlows](const CallArgs& a) -> FormulaValue {
            FormulaValue error;
            if (a.ScalarError(0, error)) return error;
            const double r = a.Number(0);
            if (r <= -1) return Err(CellErrorType::NumError);
            std::vector<double> flows, days;
            if (!datedFlows(a, 1, 2, flows, days, error)) return error;
            double sum = 0.0;
            for (size_t k = 0; k < flows.size(); ++k) sum += flows[k] / std::pow(1 + r, (days[k] - days[0]) / 365.0);
            return Num(sum);
        });
    // XIRR: Newton's method within 1E-8 (Excel's 0.000001 percent) in at
    // most 100 steps, as Excel documents it.
    Add(lib, "XIRR", 2, 3, "Financial", "Internal rate of return of dated cash flows",
        [datedFlows](const CallArgs& a) -> FormulaValue {
            std::vector<double> flows, days;
            FormulaValue error;
            if (!datedFlows(a, 0, 1, flows, days, error)) return error;
            if (!HasBothSigns(flows)) return Err(CellErrorType::NumError);
            double r = a.Omitted(2) ? 0.1 : a.Number(2);
            if (r <= -1) return Err(CellErrorType::NumError);
            for (int i = 0; i < 100; ++i) {
                double value = 0.0, slope = 0.0;
                for (size_t k = 0; k < flows.size(); ++k) {
                    const double years = (days[k] - days[0]) / 365.0;
                    const double d = std::pow(1 + r, years);
                    value += flows[k] / d;
                    slope -= years * flows[k] / (d * (1 + r));
                }
                if (slope == 0 || !std::isfinite(slope)) return Err(CellErrorType::NumError);
                double next = r - value / slope;
                if (!std::isfinite(next)) return Err(CellErrorType::NumError);
                if (next <= -1) next = (r - 1) / 2;   // stay above -100%
                if (std::fabs(next - r) < 1e-8) return Num(next);
                r = next;
            }
            return Err(CellErrorType::NumError);
        });

    // MIRR(values, finance_rate, reinvest_rate)
    Add(lib, "MIRR", 3, 3, "Financial", "Modified internal rate of return", [](const CallArgs& a) -> FormulaValue {
        std::vector<double> flows;
        FormulaValue error;
        if (!CashFlows(a, 0, flows, error)) return error;
        if (a.ScalarError(1, error)) return error;
        const double finance = a.Number(1), reinvest = a.Number(2);
        const double n = static_cast<double>(flows.size());
        if (flows.size() < 2) return Err(CellErrorType::DivisionByZero);
        double positive = 0.0, negative = 0.0;
        for (size_t k = 0; k < flows.size(); ++k) {
            const double i = static_cast<double>(k);
            if (flows[k] > 0) positive += flows[k] * std::pow(1 + reinvest, n - 1 - i);
            else negative += flows[k] / std::pow(1 + finance, i);
        }
        if (positive == 0 || negative == 0) return Err(CellErrorType::DivisionByZero);
        return Num(std::pow(-positive / negative, 1.0 / (n - 1)) - 1);
    });

    // SLN(cost, salvage, life) and SYD(cost, salvage, life, per)
    Add(lib, "SLN", 3, 3, "Financial", "Straight-line depreciation for one period", [](const CallArgs& a) -> FormulaValue {
        FormulaValue error;
        if (a.ScalarError(0, error)) return error;
        const double life = a.Number(2);
        if (life == 0) return Err(CellErrorType::DivisionByZero);
        return Num((a.Number(0) - a.Number(1)) / life);
    });
    Add(lib, "SYD", 4, 4, "Financial", "Sum-of-years' digits depreciation for a period", [](const CallArgs& a) -> FormulaValue {
        FormulaValue error;
        if (a.ScalarError(0, error)) return error;
        const double cost = a.Number(0), salvage = a.Number(1), life = a.Number(2), per = a.Number(3);
        if (life <= 0 || per <= 0 || per > life) return Err(CellErrorType::NumError);
        return Num((cost - salvage) * (life - per + 1) * 2 / (life * (life + 1)));
    });

    // DB(cost, salvage, life, period, [month]): fixed-declining balance, the
    // rate rounded to three decimals.
    Add(lib, "DB", 4, 5, "Financial", "Fixed-declining balance depreciation", [](const CallArgs& a) -> FormulaValue {
        FormulaValue error;
        if (a.ScalarError(0, error)) return error;
        const double cost = a.Number(0), salvage = a.Number(1), life = a.Number(2), period = a.Number(3);
        const double months = a.Omitted(4) ? 12.0 : a.Number(4);
        if (months < 1 || months > 12 || life > 1200 || salvage < 0 || period > life + 1 || salvage > cost ||
            cost <= 0 || life <= 0 || period <= 0) {
            return Err(CellErrorType::NumError);
        }
        double rate = 1.0 - std::pow(salvage / cost, 1.0 / life);
        rate = std::floor(rate * 1000.0 + 0.5) / 1000.0;
        const double first = cost * rate * months / 12.0;
        if (std::floor(period) == 1) return Num(first);
        double total = first, db = 0.0;
        const int last = static_cast<int>(std::floor(std::min(life, period)));
        for (int i = 2; i <= last; ++i) {
            db = (cost - total) * rate;
            total += db;
        }
        if (period > life) db = (cost - total) * rate * (12.0 - months) / 12.0;
        return Num(db);
    });

    // DDB(cost, salvage, life, period, [factor])
    Add(lib, "DDB", 4, 5, "Financial", "Double-declining balance depreciation", [](const CallArgs& a) -> FormulaValue {
        FormulaValue error;
        if (a.ScalarError(0, error)) return error;
        const double cost = a.Number(0), salvage = a.Number(1), life = a.Number(2), period = a.Number(3);
        const double factor = a.Omitted(4) ? 2.0 : a.Number(4);
        if (cost < 0 || salvage < 0 || factor <= 0 || salvage > cost || period < 1 || period > life || life <= 0) {
            return Err(CellErrorType::NumError);
        }
        return Num(DecliningStep(cost, salvage, life, period, factor));
    });

    // VDB(cost, salvage, life, start_period, end_period, [factor], [no_switch])
    Add(lib, "VDB", 5, 7, "Financial", "Declining balance depreciation between two periods",
        [](const CallArgs& a) -> FormulaValue {
            FormulaValue error;
            if (a.ScalarError(0, error)) return error;
            double cost = a.Number(0);
            const double salvage = a.Number(1), life = a.Number(2), start = a.Number(3), end = a.Number(4);
            const double factor = a.Omitted(5) ? 2.0 : a.Number(5);
            const bool noSwitch = !a.Omitted(6) && a.Value(6).ToNumber() != 0.0;
            if (start < 0 || end < start || end > life || cost < 0 || salvage > cost || factor <= 0 || life <= 0) {
                return Err(CellErrorType::NumError);
            }
            const double intStart = std::floor(start), intEnd = std::ceil(end);
            double vdb = 0.0;
            if (noSwitch) {
                const long loopStart = static_cast<long>(intStart), loopEnd = static_cast<long>(intEnd);
                for (long i = loopStart + 1; i <= loopEnd; ++i) {
                    double term = DecliningStep(cost, salvage, life, static_cast<double>(i), factor);
                    if (i == loopStart + 1) term *= std::min(end, intStart + 1.0) - start;
                    else if (i == loopEnd) term *= end + 1.0 - intEnd;
                    vdb += term;
                }
                return Num(vdb);
            }
            double partial = 0.0;
            if (start > intStart) {   // the part of the first period before start
                const double left = cost - VdbWhole(cost, salvage, life, life, intStart, factor);
                partial += (start - intStart) * VdbWhole(left, salvage, life, life - intStart, 1.0, factor);
            }
            if (end < intEnd) {   // the part of the last period after end
                const double tempStart = intEnd - 1.0;
                const double left = cost - VdbWhole(cost, salvage, life, life, tempStart, factor);
                partial += (intEnd - end) * VdbWhole(left, salvage, life, life - tempStart, intEnd - tempStart, factor);
            }
            cost -= VdbWhole(cost, salvage, life, life, intStart, factor);
            vdb = VdbWhole(cost, salvage, life, life - intStart, intEnd - intStart, factor);
            return Num(vdb - partial);
        });

    // EFFECT(nominal_rate, npery) and NOMINAL(effect_rate, npery)
    Add(lib, "EFFECT", 2, 2, "Financial", "Effective annual interest rate", [](const CallArgs& a) -> FormulaValue {
        FormulaValue error;
        if (a.ScalarError(0, error)) return error;
        const double rate = a.Number(0), periods = std::trunc(a.Number(1));
        if (rate <= 0 || periods < 1) return Err(CellErrorType::NumError);
        return Num(std::pow(1 + rate / periods, periods) - 1);
    });
    Add(lib, "NOMINAL", 2, 2, "Financial", "Nominal annual interest rate", [](const CallArgs& a) -> FormulaValue {
        FormulaValue error;
        if (a.ScalarError(0, error)) return error;
        const double rate = a.Number(0), periods = std::trunc(a.Number(1));
        if (rate <= 0 || periods < 1) return Err(CellErrorType::NumError);
        return Num((std::pow(1 + rate, 1.0 / periods) - 1) * periods);
    });
}

} // namespace

// The functions that take single values only: an array or a range given for
// one of their arguments calls them once per element (=ABS(A1:A3)).
void FormulaFunctionLibrary::ApplyArrayBehaviour() {
    static const char* const kElementWise[] = {
        "ABS", "ACOS", "ASIN", "ATAN", "ATAN2", "CEILING", "CHAR", "CLEAN", "CODE", "COMBIN", "COS",
        "COSH", "DATE", "DATEDIF", "DATEVALUE", "DAY", "DAYS", "DEGREES", "EDATE", "EOMONTH", "EVEN",
        "EXACT", "EXP", "FACT", "FIND", "FLOOR", "HOUR", "IF", "IFERROR", "IFNA", "INT", "ISBLANK",
        "ISERR", "ISERROR", "ISEVEN", "ISLOGICAL", "ISNA", "ISNONTEXT", "ISNUMBER", "ISODD", "ISTEXT",
        "LEFT", "LEN", "LN", "LOG", "LOG10", "LOWER", "MID", "MINUTE", "MOD", "MONTH", "MROUND", "N",
        "NOT", "ODD", "POWER", "PROPER", "QUOTIENT", "RADIANS", "REPLACE", "REPT", "RIGHT", "ROUND",
        "ROUNDDOWN", "ROUNDUP", "SEARCH", "SECOND", "SIGN", "SIN", "SINH", "SQRT", "SUBSTITUTE", "T",
        "TAN", "TANH", "TEXT", "TIME", "TRIM", "TRUNC", "UPPER", "VALUE", "WEEKDAY", "YEAR",
    };
    for (const char* name : kElementWise) {
        auto it = functions_.find(name);
        if (it != functions_.end()) it->second.elementWise = true;
    }
}

void FormulaFunctionLibrary::RegisterExcelFunctions() {
    RegisterLookups(*this);
    RegisterConditional(*this);
    RegisterMath(*this);
    RegisterStatistics(*this);
    RegisterText(*this);
    RegisterDates(*this);
    RegisterLogicAndInformation(*this);
    RegisterFinance(*this);
    RegisterMoreFinance(*this);
    RegisterDynamicArrays(*this);
}


// ============================================================================
// ARGUMENT NAMES
// ============================================================================

// The argument names every built-in function shows in its signature and in
// the parameter hint while a formula is typed - the names Excel uses. "?"
// marks an optional argument; a function that takes more arguments than are
// named here repeats the last (or last pair), shown as "...".
void FormulaFunctionLibrary::ApplyArgumentNames() {
    static const std::pair<const char*, const char*> kArguments[] = {
        { "ABS", "number" }, { "ACOS", "number" }, { "AND", "logical1, logical2?" },
        { "ASIN", "number" }, { "ATAN", "number" }, { "ATAN2", "x_num, y_num" },
        { "AVERAGE", "number1, number2?" }, { "AVERAGEIF", "range, criteria, average_range?" },
        { "AVERAGEIFS", "average_range, criteria_range1, criteria1" },
        { "CEILING", "number, significance?" }, { "CHAR", "number" },
        { "CHOOSE", "index_num, value1" }, { "CLEAN", "text" }, { "CODE", "text" },
        { "COLUMNS", "array" }, { "COMBIN", "number, number_chosen" },
        { "CONCAT", "text1, text2?" }, { "CONCATENATE", "text1, text2?" },
        { "COS", "number" }, { "COSH", "number" }, { "COUNT", "value1, value2?" },
        { "COUNTA", "value1, value2?" }, { "COUNTBLANK", "range" },
        { "COUNTIF", "range, criteria" }, { "COUNTIFS", "criteria_range1, criteria1" },
        { "DATE", "year, month, day" }, { "DATEDIF", "start_date, end_date, unit" },
        { "DATEVALUE", "date_text" }, { "DAY", "serial_number" },
        { "DAYS", "end_date, start_date" }, { "DEGREES", "angle" },
        { "EDATE", "start_date, months" }, { "EOMONTH", "start_date, months" },
        { "EVEN", "number" }, { "EXACT", "text1, text2" }, { "EXP", "number" },
        { "FACT", "number" }, { "FALSE", "" }, { "FIND", "find_text, within_text, start_num?" },
        { "FLOOR", "number, significance?" }, { "FV", "rate, nper, pmt, pv?, type?" },
        { "GCD", "number1, number2?" },
        { "HLOOKUP", "lookup_value, table_array, row_index_num, range_lookup?" },
        { "HOUR", "serial_number" }, { "IF", "logical_test, value_if_true, value_if_false?" },
        { "IFERROR", "value, value_if_error" }, { "IFNA", "value, value_if_na" },
        { "IFS", "logical_test1, value_if_true1" }, { "INDEX", "array, row_num, column_num?" },
        { "INT", "number" }, { "ISBLANK", "value" }, { "ISERR", "value" }, { "ISERROR", "value" },
        { "ISEVEN", "number" }, { "ISLOGICAL", "value" }, { "ISNA", "value" },
        { "ISNONTEXT", "value" }, { "ISNUMBER", "value" }, { "ISODD", "number" },
        { "ISTEXT", "value" }, { "LARGE", "array, k" }, { "LCM", "number1, number2?" },
        { "LEFT", "text, num_chars?" }, { "LEN", "text" }, { "LN", "number" },
        { "LOG", "number, base?" }, { "LOG10", "number" },
        { "LOOKUP", "lookup_value, lookup_vector, result_vector?" }, { "LOWER", "text" },
        { "MATCH", "lookup_value, lookup_array, match_type?" }, { "MAX", "number1, number2?" },
        { "MAXIFS", "max_range, criteria_range1, criteria1" }, { "MEDIAN", "number1, number2?" },
        { "MID", "text, start_num, num_chars" }, { "MIN", "number1, number2?" },
        { "MINIFS", "min_range, criteria_range1, criteria1" }, { "MINUTE", "serial_number" },
        { "MOD", "number, divisor" }, { "MODE", "number1, number2?" },
        { "MODE.SNGL", "number1, number2?" }, { "MONTH", "serial_number" },
        { "MROUND", "number, multiple" }, { "N", "value" }, { "NA", "" }, { "NOT", "logical" },
        { "NOW", "" }, { "NPV", "rate, value1" }, { "ODD", "number" },
        { "OR", "logical1, logical2?" }, { "PERCENTILE", "array, k" }, { "PI", "" },
        { "PMT", "rate, nper, pv, fv?, type?" }, { "POWER", "number, power" },
        { "PRODUCT", "number1, number2?" }, { "PROPER", "text" },
        { "PV", "rate, nper, pmt, fv?, type?" }, { "QUARTILE", "array, quart" },
        { "QUOTIENT", "numerator, denominator" }, { "RADIANS", "angle" }, { "RAND", "" },
        { "RANDBETWEEN", "bottom, top" }, { "RANK", "number, ref, order?" },
        { "RANK.EQ", "number, ref, order?" },
        { "REPLACE", "old_text, start_num, num_chars, new_text" },
        { "REPT", "text, number_times" }, { "RIGHT", "text, num_chars?" },
        { "ROUND", "number, num_digits" }, { "ROUNDDOWN", "number, num_digits" },
        { "ROUNDUP", "number, num_digits" }, { "ROWS", "array" },
        { "SEARCH", "find_text, within_text, start_num?" }, { "SECOND", "serial_number" },
        { "SIGN", "number" }, { "SIN", "number" }, { "SINH", "number" }, { "SMALL", "array, k" },
        { "SQRT", "number" }, { "STDEV", "number1, number2?" }, { "STDEV.P", "number1, number2?" },
        { "STDEV.S", "number1, number2?" }, { "STDEVP", "number1, number2?" },
        { "SUBSTITUTE", "text, old_text, new_text, instance_num?" },
        { "SUBTOTAL", "function_num, ref1" }, { "SUM", "number1, number2?" },
        { "SUMIF", "range, criteria, sum_range?" },
        { "SUMIFS", "sum_range, criteria_range1, criteria1" },
        { "SUMPRODUCT", "array1, array2?" }, { "SUMSQ", "number1, number2?" },
        { "SWITCH", "expression, value1, result1" }, { "T", "value" }, { "TAN", "number" },
        { "TANH", "number" }, { "TEXT", "value, format_text" },
        { "TEXTJOIN", "delimiter, ignore_empty, text1" }, { "TIME", "hour, minute, second" },
        { "TODAY", "" }, { "TRIM", "text" }, { "TRUE", "" }, { "TRUNC", "number, num_digits?" },
        { "UPPER", "text" }, { "VALUE", "text" }, { "VAR", "number1, number2?" },
        { "VAR.P", "number1, number2?" }, { "VAR.S", "number1, number2?" },
        { "VARP", "number1, number2?" },
        { "VLOOKUP", "lookup_value, table_array, col_index_num, range_lookup?" },
        { "WEEKDAY", "serial_number, return_type?" },
        { "XLOOKUP", "lookup_value, lookup_array, return_array, if_not_found?, match_mode?, search_mode?" },
        { "XOR", "logical1, logical2?" }, { "YEAR", "serial_number" },
        // Dynamic arrays and LAMBDA
        { "FILTER", "array, include, if_empty?" },
        { "SORT", "array, sort_index?, sort_order?, by_col?" },
        { "SORTBY", "array, by_array1, sort_order1?" }, { "UNIQUE", "array, by_col?, exactly_once?" },
        { "SEQUENCE", "rows, columns?, start?, step?" },
        { "RANDARRAY", "rows?, columns?, min?, max?, whole_number?" }, { "TRANSPOSE", "array" },
        { "XMATCH", "lookup_value, lookup_array, match_mode?, search_mode?" },
        { "LET", "name1, name_value1, calculation_or_name2" }, { "LAMBDA", "parameter_or_calculation" },
        { "MAP", "array1, lambda_or_array2" }, { "BYROW", "array, lambda" }, { "BYCOL", "array, lambda" },
        { "REDUCE", "initial_value?, array, lambda" }, { "SCAN", "initial_value?, array, lambda" },
        { "MAKEARRAY", "rows, cols, lambda" },
        // Financial
        { "NPER", "rate, pmt, pv, fv?, type?" }, { "RATE", "nper, pmt, pv, fv?, type?, guess?" },
        { "IPMT", "rate, per, nper, pv, fv?, type?" }, { "PPMT", "rate, per, nper, pv, fv?, type?" },
        { "CUMIPMT", "rate, nper, pv, start_period, end_period, type" },
        { "CUMPRINC", "rate, nper, pv, start_period, end_period, type" },
        { "IRR", "values, guess?" }, { "XIRR", "values, dates, guess?" },
        { "XNPV", "rate, values, dates" }, { "MIRR", "values, finance_rate, reinvest_rate" },
        { "SLN", "cost, salvage, life" }, { "SYD", "cost, salvage, life, per" },
        { "DB", "cost, salvage, life, period, month?" }, { "DDB", "cost, salvage, life, period, factor?" },
        { "VDB", "cost, salvage, life, start_period, end_period, factor?, no_switch?" },
        { "EFFECT", "nominal_rate, npery" }, { "NOMINAL", "effect_rate, npery" },
    };
    for (const auto& [name, spec] : kArguments) {
        auto it = functions_.find(name);
        if (it == functions_.end()) continue;
        std::vector<FunctionArgDef> args;
        std::stringstream in(spec);
        std::string part;
        while (std::getline(in, part, ',')) {
            const size_t b = part.find_first_not_of(' ');
            if (b == std::string::npos) continue;
            part = part.substr(b);
            FunctionArgDef arg;
            arg.optional = !part.empty() && part.back() == '?';
            arg.name = arg.optional ? part.substr(0, part.size() - 1) : part;
            args.push_back(arg);
        }
        it->second.args = std::move(args);
    }
}

} // namespace UltraCanvas
