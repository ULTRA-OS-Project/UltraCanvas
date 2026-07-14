// Tests/SpreadsheetNumberFormatTest.cpp
// Regression test for spreadsheet number-format display.
//
// A real-world accounting workbook renders currency, percentage and date cells
// using the document's number:*-style definitions (symbol position, thousands
// grouping, red negatives, date component order). The ODS loader translates
// those into NumberFormat; this test verifies the display side actually honours
// them, and that DateTimeValue round-trips a date without drifting a day.
//
// Header-only: exercises the real SpreadsheetCell / NumberFormat / DateTimeValue
// with no UI stack (see Tests/CMakeLists.txt).
#include "UltraCanvasSpreadsheetCell.h"

#include <iostream>
#include <string>

using namespace UltraCanvas;

static int failures = 0;

#define CHECK_EQ(actual, expected) do { \
    auto _a = (actual); auto _e = (expected); \
    if (!(_a == _e)) { \
        std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ \
                  << "  " #actual " == " #expected \
                  << "  [got \"" << _a << "\", want \"" << _e << "\"]\n"; \
        ++failures; \
    } \
} while (0)

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; \
        ++failures; \
    } \
} while (0)

// Euro sign as UTF-8.
static const std::string EUR = "\xE2\x82\xAC";

static std::string DisplayCurrency(double amount, const NumberFormat& f) {
    SpreadsheetCell c;
    c.SetCurrency(amount, "EUR");
    CellStyle s; s.numberFormat = f; c.SetStyle(s);
    return c.GetDisplayValue();
}

static NumberFormat Cur(const std::string& symbol, bool after, int dec,
                        bool thousands, bool negRed) {
    NumberFormat f;
    f.category = NumberFormatCategory::Currency;
    f.currencySymbol = symbol;
    f.currencySymbolAfter = after;
    f.decimalPlaces = dec;
    f.useThousandsSeparator = thousands;
    f.negativeRed = negRed;
    return f;
}

// ---------------------------------------------------------------------------
// Currency: symbol position, grouping, decimals, sign
// ---------------------------------------------------------------------------
static void TestCurrency() {
    // Symbol after with a space: "2,397.36 €" (04 2026 tab).
    NumberFormat after = Cur(EUR, true, 2, true, false);
    CHECK_EQ(DisplayCurrency(2397.36, after), "2,397.36 " + EUR);
    CHECK_EQ(DisplayCurrency(500.0, after),    "500.00 " + EUR);

    // Symbol before with a space, red negatives: "€ 2,397.36" / "-€ 121.50"
    // (Total tab). The loader folds the separating space into the symbol.
    NumberFormat before = Cur(EUR + " ", false, 2, true, true);
    CHECK_EQ(DisplayCurrency(2397.36, before), EUR + " 2,397.36");
    CHECK_EQ(DisplayCurrency(-121.50, before), "-" + EUR + " 121.50");
    CHECK(before.negativeRed);

    // Without a parsed currency format, the display falls back to "CODE amount"
    // using General number formatting for the amount.
    SpreadsheetCell plain; plain.SetCurrency(1234.5, "EUR");
    CHECK_EQ(plain.GetDisplayValue(), std::string("EUR 1234.5"));
}

// ---------------------------------------------------------------------------
// Percentage: stored fraction rendered as a percent
// ---------------------------------------------------------------------------
static void TestPercentage() {
    SpreadsheetCell c;
    c.SetPercentage(0.0936);           // ODS stores the fraction
    CellStyle s;
    s.numberFormat.category = NumberFormatCategory::Percentage;
    s.numberFormat.decimalPlaces = 2;
    c.SetStyle(s);
    CHECK_EQ(c.GetDisplayValue(), std::string("9.36%"));
}

// ---------------------------------------------------------------------------
// Date: honour the format code parsed from number:date-style
// ---------------------------------------------------------------------------
static std::string DisplayDate(int y, int m, int d, const std::string& code) {
    SpreadsheetCell c;
    c.SetDate(y, m, d);
    CellStyle s;
    s.numberFormat.category = NumberFormatCategory::Date;
    s.numberFormat.formatCode = code;
    c.SetStyle(s);
    return c.GetDisplayValue();
}

static void TestDateFormat() {
    CHECK_EQ(DisplayDate(2025, 1, 15, "MM/YYYY"),  std::string("01/2025"));
    CHECK_EQ(DisplayDate(2025, 1, 15, "MM / YYYY"), std::string("01 / 2025"));
    CHECK_EQ(DisplayDate(2026, 4, 1,  "DD/MM/YY"), std::string("01/04/26"));
    CHECK_EQ(DisplayDate(2026, 4, 8,  "D/M/YY"),   std::string("8/4/26"));
}

// ---------------------------------------------------------------------------
// DateTimeValue must round-trip a calendar date exactly (previously ToDate was
// one day ahead of FromDate, so every stored date rendered a day late).
// ---------------------------------------------------------------------------
static void TestDateRoundTrip() {
    struct { int y, m, d; } dates[] = {
        {1900,1,1}, {1970,1,1}, {2000,2,29}, {2024,2,29},
        {2025,1,1}, {2026,4,1}, {2026,12,31}, {2026,7,12},
    };
    for (auto& t : dates) {
        DateTimeValue v = DateTimeValue::FromDate(t.y, t.m, t.d);
        int y, m, d; v.ToDate(y, m, d);
        CHECK(y == t.y && m == t.m && d == t.d);
    }
    // Absolute serial matches the OpenDocument/Excel 1899-12-30 epoch.
    CHECK(DateTimeValue::FromDate(2025, 1, 1).serialNumber == 45658.0);
}

int main() {
    std::cout << "SpreadsheetNumberFormatTest\n";
    TestCurrency();
    TestPercentage();
    TestDateFormat();
    TestDateRoundTrip();

    if (failures == 0) {
        std::cout << "  All number-format display checks passed.\n";
        return 0;
    }
    std::cerr << "  " << failures << " check(s) failed.\n";
    return 1;
}
