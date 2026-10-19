// Tests/SpreadsheetSheetNameTest.cpp
// The rules behind the sheet tab menu's Rename: CheckSheetName, which says
// why a name cannot be a sheet's, and RenameSheetInFormula, which makes
// every formula naming the renamed sheet follow it, and RemoveSheetFromFormula,
// which makes references to a deleted sheet #REF!. Runs on the sheet code
// alone, with no UI stack.
#include "UltraCanvasSpreadsheetSheet.h"

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

static void TestCheckSheetName() {
    CHECK_EQ(CheckSheetName("Sales"), std::string());
    CHECK_EQ(CheckSheetName("Sales 2016 (EUR)"), std::string());
    CHECK_EQ(CheckSheetName("Umsätze"), std::string());
    CHECK_EQ(CheckSheetName(std::string(31, 'x')), std::string());
    CHECK_EQ(CheckSheetName("").empty(), false);
    CHECK_EQ(CheckSheetName(std::string(32, 'x')).empty(), false);
    // 31 characters, more bytes: still fine.
    std::string umlauts;
    for (int i = 0; i < 31; ++i) umlauts += "ä";
    CHECK_EQ(CheckSheetName(umlauts), std::string());
    for (const char* bad : { "A/B", "A\\B", "A:B", "A*", "A?", "[A]", "'A", "A'" }) {
        CHECK_EQ(CheckSheetName(bad).empty(), false);
    }
}

static void TestRenameSheetInFormula() {
    CHECK_EQ(RenameSheetInFormula("=Sales.A1*2", "Sales", "Revenue"), std::string("=Revenue.A1*2"));
    CHECK_EQ(RenameSheetInFormula("=SUM(Sales!B2:B8)", "Sales", "Revenue"), std::string("=SUM(Revenue!B2:B8)"));
    // Case-insensitive, and every occurrence.
    CHECK_EQ(RenameSheetInFormula("=sales.A1+SALES.$B$2", "Sales", "R"), std::string("=R.A1+R.$B$2"));
    // A new name with a space is quoted; a quoted old name is found.
    CHECK_EQ(RenameSheetInFormula("=Sales.A1", "Sales", "Sales 2016"), std::string("='Sales 2016'.A1"));
    CHECK_EQ(RenameSheetInFormula("='Sales 2016'.A1", "Sales 2016", "Old"), std::string("=Old.A1"));
    CHECK_EQ(RenameSheetInFormula("=Sales.A1", "Sales", "Bob's"), std::string("='Bob''s'.A1"));
    CHECK_EQ(RenameSheetInFormula("='Bob''s'.A1", "Bob's", "B"), std::string("=B.A1"));
    // Left alone: other sheets, longer names, text, the current sheet's cells.
    CHECK_EQ(RenameSheetInFormula("=Sales2.A1+MySales.A1", "Sales", "R"), std::string("=Sales2.A1+MySales.A1"));
    CHECK_EQ(RenameSheetInFormula("=\"Sales.A1\"&A1", "Sales", "R"), std::string("=\"Sales.A1\"&A1"));
    CHECK_EQ(RenameSheetInFormula("=A1+B2", "Sales", "R"), std::string("=A1+B2"));
    // A number is not a sheet called "2016".
    CHECK_EQ(RenameSheetInFormula("=2016.5*'2016'.A1", "2016", "Y"), std::string("=2016.5*Y.A1"));
}

static void TestRemoveSheetFromFormula() {
    CHECK_EQ(RemoveSheetFromFormula("=Sales.A1*2", "Sales"), std::string("=#REF!*2"));
    CHECK_EQ(RemoveSheetFromFormula("=Sales!$B$2+1", "Sales"), std::string("=#REF!+1"));
    CHECK_EQ(RemoveSheetFromFormula("=SUM(sales.A1:B3)", "Sales"), std::string("=SUM(#REF!)"));
    CHECK_EQ(RemoveSheetFromFormula("=SUM('Q 1'.A1:'Q 1'.C9)", "Q 1"), std::string("=SUM(#REF!)"));
    CHECK_EQ(RemoveSheetFromFormula("=Sales.A1+Costs.A1", "Sales"), std::string("=#REF!+Costs.A1"));
    // Left alone: other sheets, longer names, text, plain references, numbers.
    CHECK_EQ(RemoveSheetFromFormula("=Sales2.A1+MySales.B2", "Sales"), std::string("=Sales2.A1+MySales.B2"));
    CHECK_EQ(RemoveSheetFromFormula("=\"Sales.A1\"&A1", "Sales"), std::string("=\"Sales.A1\"&A1"));
    CHECK_EQ(RemoveSheetFromFormula("=A1:B2*2016.5", "2016"), std::string("=A1:B2*2016.5"));
}

int main() {
    TestCheckSheetName();
    TestRenameSheetInFormula();
    TestRemoveSheetFromFormula();

    if (failures == 0) {
        std::cout << "SpreadsheetSheetNameTest: all checks passed\n";
        return 0;
    }
    std::cerr << "SpreadsheetSheetNameTest: " << failures << " failure(s)\n";
    return 1;
}
