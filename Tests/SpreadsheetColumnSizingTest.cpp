// Tests/SpreadsheetColumnSizingTest.cpp
// Regression test for spreadsheet column sizing.
//
// Two things went wrong before: imported documents lost their column widths
// (ODF stores the width in a table-column style, not on the element, and the
// unit conversion was ad hoc), and a file that carries no widths at all - every
// CSV, plus plenty of ODS/XLSX documents - displayed every column at the same
// default width, clipping the values. This test covers the pieces that fixed
// both: length conversion, UTF-8-aware content measurement, and the auto-width
// pass that leaves document-sized columns alone.
//
// Exercises the real SpreadsheetSheet / metrics helpers with no UI stack
// (see Tests/CMakeLists.txt).
#include "UltraCanvasSpreadsheetSheet.h"

#include <cmath>
#include <iostream>
#include <string>

using namespace UltraCanvas;

static int failures = 0;

#define CHECK_EQ(actual, expected) do { \
    auto _a = (actual); auto _e = (expected); \
    if (!(_a == _e)) { \
        std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ \
                  << "  " #actual " == " #expected \
                  << "  [got " << _a << ", want " << _e << "]\n"; \
        ++failures; \
    } \
} while (0)

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; \
        ++failures; \
    } \
} while (0)

// Euro sign as UTF-8 (three bytes, one character).
static const std::string EUR = "\xE2\x82\xAC";

// ---------------------------------------------------------------------------
// Document lengths
// ---------------------------------------------------------------------------
static void TestLengthConversion() {
    // 96 px per inch is the reference the grid is authored against.
    CHECK_EQ(SpreadsheetLengthToPixels("1in", 0), 96);
    CHECK_EQ(SpreadsheetLengthToPixels("2.54cm", 0), 96);
    CHECK_EQ(SpreadsheetLengthToPixels("25.4mm", 0), 96);
    CHECK_EQ(SpreadsheetLengthToPixels("72pt", 0), 96);
    CHECK_EQ(SpreadsheetLengthToPixels("6pc", 0), 96);
    CHECK_EQ(SpreadsheetLengthToPixels("120px", 0), 120);
    // A bare number is taken as pixels; ODF always writes a unit, but some
    // producers do not.
    CHECK_EQ(SpreadsheetLengthToPixels("64", 0), 64);

    // Anything unusable falls back to the caller's value rather than throwing
    // or silently producing a zero-width column.
    CHECK_EQ(SpreadsheetLengthToPixels("", 64), 64);
    CHECK_EQ(SpreadsheetLengthToPixels("auto", 64), 64);
    CHECK_EQ(SpreadsheetLengthToPixels("2em", 64), 64);
    CHECK_EQ(SpreadsheetLengthToPixels("0cm", 64), 64);
    CHECK_EQ(SpreadsheetLengthToPixels("-3cm", 64), 64);

    // Round-trips through the ODF writer's centimetre form.
    for (int pixels : { 40, 64, 97, 128, 240 }) {
        std::string written = std::to_string(SpreadsheetPixelsToCentimetres(pixels)) + "cm";
        int readBack = SpreadsheetLengthToPixels(written, 0);
        CHECK(std::abs(readBack - pixels) <= 1);
    }
}

// ---------------------------------------------------------------------------
// Content measurement
// ---------------------------------------------------------------------------
static void TestTextMeasurement() {
    // Characters, not bytes: "1.234,00 €" is 10 characters in 12 bytes.
    CHECK_EQ(SpreadsheetUtf8Length("1.234,00 " + EUR), static_cast<size_t>(10));
    CHECK_EQ(SpreadsheetUtf8Length("Chargebacks"), static_cast<size_t>(11));
    CHECK_EQ(SpreadsheetUtf8Length(""), static_cast<size_t>(0));

    CellFont font;                  // 11pt regular
    CellFont bold; bold.bold = true;
    CellFont large; large.size = 22.0f;

    // A currency string must not be sized as if it were three characters longer
    // than it is - that is what made every euro column too wide.
    CHECK(SpreadsheetEstimateTextWidth("1.234,00 " + EUR, font) <
          SpreadsheetEstimateTextWidth("1.234,00 EUR!", font));
    CHECK(SpreadsheetEstimateTextWidth("Sales", bold) >
          SpreadsheetEstimateTextWidth("Sales", font));
    CHECK(SpreadsheetEstimateTextWidth("Sales", large) >
          SpreadsheetEstimateTextWidth("Sales", font));
    CHECK_EQ(SpreadsheetEstimateTextWidth("", font), 0);

    // The clamp keeps auto-fitted columns inside a usable range.
    CHECK_EQ(SpreadsheetClampAutoFitWidth(2), SpreadsheetAutoFit::MinWidth);
    CHECK_EQ(SpreadsheetClampAutoFitWidth(100000), SpreadsheetAutoFit::MaxWidth);
    CHECK_EQ(SpreadsheetClampAutoFitWidth(100), 100 + SpreadsheetAutoFit::Padding);
}

// ---------------------------------------------------------------------------
// Auto-width pass
// ---------------------------------------------------------------------------

// A deterministic stand-in for the render context's text metrics: 10 pixels per
// character, so expected widths can be written down exactly.
static int TenPxPerChar(const std::string& text, const CellFont&) {
    return static_cast<int>(SpreadsheetUtf8Length(text)) * 10;
}

static void TestAutoFitUnsizedColumns() {
    SpreadsheetSheet sheet("Sales");

    sheet.GetCell(0, 0)->SetText("Month");             //  5 chars
    sheet.GetCell(1, 0)->SetText("06.2016");           //  7 chars -> widest in A
    sheet.GetCell(0, 1)->SetText("Chargebacks");       // 11 chars
    sheet.GetCell(0, 2)->SetText("Sales (" + EUR + ")");  // 9 characters, 11 bytes

    // Column B was sized by the document; the pass must not touch it.
    sheet.SetColumnWidth(1, 55, /*explicitWidth*/ true);
    CHECK(sheet.HasExplicitColumnWidth(1));
    CHECK(!sheet.HasExplicitColumnWidth(0));

    sheet.AutoFitUnsizedColumns(TenPxPerChar);

    CHECK_EQ(sheet.GetColumnWidth(0), SpreadsheetClampAutoFitWidth(7 * 10));
    CHECK_EQ(sheet.GetColumnWidth(1), 55);                       // untouched
    CHECK_EQ(sheet.GetColumnWidth(2), SpreadsheetClampAutoFitWidth(9 * 10));

    // An auto-fitted width is not "explicit", so a later pass can refine it
    // once real font metrics are available.
    CHECK(!sheet.HasExplicitColumnWidth(0));

    // Re-running with different metrics re-fits the auto columns and still
    // leaves the document-sized one alone.
    sheet.AutoFitUnsizedColumns([](const std::string& text, const CellFont&) {
        return static_cast<int>(SpreadsheetUtf8Length(text)) * 20;
    });
    CHECK_EQ(sheet.GetColumnWidth(0), SpreadsheetClampAutoFitWidth(7 * 20));
    CHECK_EQ(sheet.GetColumnWidth(1), 55);
}

static void TestAutoFitSingleColumn() {
    SpreadsheetSheet sheet("Sheet1");
    sheet.GetCell(0, 0)->SetText("Chargebacks");   // 11 chars

    sheet.AutoFitColumnWidth(0, TenPxPerChar);
    CHECK_EQ(sheet.GetColumnWidth(0), SpreadsheetClampAutoFitWidth(11 * 10));

    // With no measurer the estimate is used - the column must still grow past
    // the default rather than staying put.
    SpreadsheetSheet estimated("Sheet1");
    estimated.GetCell(0, 0)->SetText("A rather long piece of cell text");
    estimated.AutoFitColumnWidth(0);
    CHECK(estimated.GetColumnWidth(0) > SpreadsheetLimits::DefaultColumnWidth);

    // An empty column keeps the sheet default instead of collapsing.
    SpreadsheetSheet empty("Sheet1");
    empty.GetCell(0, 3)->SetText("x");
    empty.AutoFitColumnWidth(1, TenPxPerChar);
    CHECK_EQ(empty.GetColumnWidth(1), empty.GetDefaultColumnWidth());
}

static void TestRowHeights() {
    SpreadsheetSheet sheet("Sheet1");
    CHECK(!sheet.HasExplicitRowHeight(0));
    sheet.SetRowHeight(0, 30);
    CHECK(sheet.HasExplicitRowHeight(0));
    CHECK_EQ(sheet.GetRowHeight(0), 30);
    sheet.SetRowHeight(1, 30, /*explicitHeight*/ false);
    CHECK(!sheet.HasExplicitRowHeight(1));

    // A row holding a large font needs a taller row than the default, or the
    // glyphs are clipped top and bottom.
    SpreadsheetSheet fit("Sheet1");
    fit.GetCell(0, 0)->SetText("Heading");
    fit.GetCell(0, 0)->GetStyleMutable()->font.size = 24.0f;
    fit.GetCell(1, 0)->SetText("body");
    fit.AutoFitRowHeight(0);
    fit.AutoFitRowHeight(1);
    CHECK(fit.GetRowHeight(0) > fit.GetDefaultRowHeight());
    CHECK(fit.GetRowHeight(0) >= 24);
    // Ordinary 11pt text keeps the sheet default rather than shrinking.
    CHECK_EQ(fit.GetRowHeight(1), fit.GetDefaultRowHeight());
    // An empty row too.
    fit.AutoFitRowHeight(5);
    CHECK_EQ(fit.GetRowHeight(5), fit.GetDefaultRowHeight());
}

int main() {
    std::cout << "SpreadsheetColumnSizingTest\n";
    TestLengthConversion();
    TestTextMeasurement();
    TestAutoFitUnsizedColumns();
    TestAutoFitSingleColumn();
    TestRowHeights();

    if (failures == 0) {
        std::cout << "  all checks passed\n";
        return 0;
    }
    std::cerr << "  " << failures << " check(s) failed\n";
    return 1;
}
