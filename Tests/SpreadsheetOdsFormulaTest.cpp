// Tests/SpreadsheetOdsFormulaTest.cpp
// Regression test for loading OpenDocument (ODS) spreadsheet formulas.
//
// A real-world accounting workbook (many sheets whose names contain spaces,
// e.g. "01 2025" / "04 2026") exercises three ODF reference forms that the
// loader must translate into the engine's native A1 syntax:
//     [.D28]                  -> D28              (current sheet, single cell)
//     [.B28:.B42]             -> B28:B42          (current sheet, range)
//     ['01 2025'.B55]         -> '01 2025'.B55    (cross-sheet, quoted name)
//     ['01 2026'.B4:.B40]     -> '01 2026'.B4:B40 (cross-sheet range)
// Before the fix, bracketed ranges and every cross-sheet reference failed to
// tokenize (the brackets, the leading '.', and the quoted sheet names were
// silently skipped), so SUM() totals and all cross-sheet pulls broke.
//
// This test drives the real production code paths without the UI stack:
//   * OdsFormulaToNative()  - the loader's ODF -> native translation
//   * FormulaTokenizer      - the engine's tokenizer (quoted sheet refs)
// It needs only header includes (see Tests/CMakeLists.txt).
#include "UltraCanvasSpreadsheetOdsFormula.h"
#include "UltraCanvasSpreadsheetFormula.h"

#include <iostream>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; \
        ++failures; \
    } \
} while (0)

#define CHECK_EQ(actual, expected) do { \
    auto _a = (actual); auto _e = (expected); \
    if (!(_a == _e)) { \
        std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ \
                  << "  " #actual " == " #expected \
                  << "  [got \"" << _a << "\", want \"" << _e << "\"]\n"; \
        ++failures; \
    } \
} while (0)

// Column letter index: A=0, B=1, ... (matches CellAddress::col).
static int Col(const char* letters) {
    return CellAddress::LetterToColumn(letters);
}

static std::vector<FormulaToken> Tok(const std::string& native) {
    FormulaTokenizer tk(native);
    return tk.Tokenize();
}

// ---------------------------------------------------------------------------
// 1. ODF -> native string translation
// ---------------------------------------------------------------------------
static void TestTranslation() {
    CHECK_EQ(OdsFormulaToNative("of:=[.D28]-[.E28]"),        std::string("=D28-E28"));
    CHECK_EQ(OdsFormulaToNative("of:=SUM([.B28:.B42])"),     std::string("=SUM(B28:B42)"));
    CHECK_EQ(OdsFormulaToNative("of:=SUM([.F4:.F16])"),      std::string("=SUM(F4:F16)"));
    CHECK_EQ(OdsFormulaToNative("of:=['01 2025'.B55]"),      std::string("='01 2025'.B55"));
    CHECK_EQ(OdsFormulaToNative("of:=SUM(['01 2026'.B4:.B40])"),
             std::string("=SUM('01 2026'.B4:B40)"));
    // Decimal points that are not inside a reference must be left alone.
    CHECK_EQ(OdsFormulaToNative("of:=199.09/169.24*10.99"),
             std::string("=199.09/169.24*10.99"));
    // A mixed expression: current-sheet range + current-sheet cell.
    CHECK_EQ(OdsFormulaToNative("of:=SUM([.B46:.B48])+[.D50]"),
             std::string("=SUM(B46:B48)+D50"));
}

// ---------------------------------------------------------------------------
// 2. Tokenizing the translated formulas yields the right references
// ---------------------------------------------------------------------------
static void TestSameSheetCell() {
    auto toks = Tok(OdsFormulaToNative("of:=[.D28]-[.E28]"));
    // D28  -  E28  EOF
    CHECK(toks.size() == 4);
    CHECK(toks[0].type == FormulaTokenType::CellRef);
    CHECK(toks[0].cellRef.col == Col("D"));
    CHECK(toks[0].cellRef.row == 27);              // 0-based row 28
    CHECK(toks[0].cellRef.sheetName.empty());
    CHECK(toks[1].type == FormulaTokenType::Operator && toks[1].text == "-");
    CHECK(toks[2].type == FormulaTokenType::CellRef);
    CHECK(toks[2].cellRef.col == Col("E"));
    CHECK(toks[2].cellRef.row == 27);
}

static void TestSameSheetRange() {
    auto toks = Tok(OdsFormulaToNative("of:=SUM([.B28:.B42])"));
    // SUM ( RANGE ) EOF
    bool foundRange = false;
    for (const auto& t : toks) {
        if (t.type == FormulaTokenType::RangeRef) {
            foundRange = true;
            CHECK(t.rangeRef.start.col == Col("B"));
            CHECK(t.rangeRef.start.row == 27);
            CHECK(t.rangeRef.end.col == Col("B"));
            CHECK(t.rangeRef.end.row == 41);       // 0-based row 42
            CHECK(t.rangeRef.start.sheetName.empty());
        }
    }
    CHECK(foundRange);
}

static void TestCrossSheetCell() {
    // The key regression: a quoted sheet name with a space.
    auto toks = Tok(OdsFormulaToNative("of:=['01 2025'.B55]"));
    CHECK(toks.size() == 2);                        // CellRef, EOF
    CHECK(toks[0].type == FormulaTokenType::CellRef);
    CHECK_EQ(toks[0].cellRef.sheetName, std::string("01 2025"));
    CHECK(toks[0].cellRef.col == Col("B"));
    CHECK(toks[0].cellRef.row == 54);              // 0-based row 55
}

static void TestCrossSheetRange() {
    auto toks = Tok(OdsFormulaToNative("of:=SUM(['01 2026'.B4:.B40])"));
    bool foundRange = false;
    for (const auto& t : toks) {
        if (t.type == FormulaTokenType::RangeRef) {
            foundRange = true;
            CHECK_EQ(t.rangeRef.start.sheetName, std::string("01 2026"));
            CHECK(t.rangeRef.start.col == Col("B"));
            CHECK(t.rangeRef.start.row == 3);      // 0-based row 4
            CHECK(t.rangeRef.end.col == Col("B"));
            CHECK(t.rangeRef.end.row == 39);       // 0-based row 40
        }
    }
    CHECK(foundRange);
}

int main() {
    std::cout << "SpreadsheetOdsFormulaTest\n";
    TestTranslation();
    TestSameSheetCell();
    TestSameSheetRange();
    TestCrossSheetCell();
    TestCrossSheetRange();

    if (failures == 0) {
        std::cout << "  All ODS formula translation/tokenization checks passed.\n";
        return 0;
    }
    std::cerr << "  " << failures << " check(s) failed.\n";
    return 1;
}
