// Tests/SpreadsheetValidationTest.cpp
// Data validation rules on the real model, with no UI stack: a rule set on a
// cell (SpreadsheetCell::SetValidation, checked by ValidateValue - which
// accepted everything until 2026-11) and a rule set on a range
// (SpreadsheetSheet::SetValidation, checked by ValidateCell), both going
// through CheckDataValidation.
#include "UltraCanvasSpreadsheetSheet.h"

#include <iostream>
#include <string>

using namespace UltraCanvas;

static int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; \
        ++failures; \
    } \
} while (0)

static DataValidation Rule(ValidationType type, ValidationOperator op,
                           const std::string& f1, const std::string& f2 = std::string()) {
    DataValidation v;
    v.type = type;
    v.op = op;
    v.formula1 = f1;
    v.formula2 = f2;
    return v;
}

// A cell holding `number` with `rule` on it.
static bool NumberPasses(const DataValidation& rule, double number) {
    SpreadsheetCell cell;
    cell.SetNumber(number);
    cell.SetValidation(rule);
    return cell.ValidateValue();
}

static bool TextPasses(const DataValidation& rule, const std::string& text) {
    SpreadsheetCell cell;
    cell.SetText(text);
    cell.SetValidation(rule);
    return cell.ValidateValue();
}

static void TestNoRule() {
    SpreadsheetCell cell;
    cell.SetText("anything");
    CHECK(cell.ValidateValue());
    cell.SetValidation(Rule(ValidationType::Any, ValidationOperator::Between, "1", "2"));
    CHECK(cell.ValidateValue());
}

static void TestWholeNumber() {
    const auto between = Rule(ValidationType::WholeNumber, ValidationOperator::Between, "1", "10");
    CHECK(NumberPasses(between, 1));
    CHECK(NumberPasses(between, 10));
    CHECK(!NumberPasses(between, 11));
    CHECK(!NumberPasses(between, 0));
    CHECK(!NumberPasses(between, 2.5));        // not whole
    CHECK(!TextPasses(between, "five"));       // text is not a number (GetNumber() gave 0 before)

    const auto notBetween = Rule(ValidationType::WholeNumber, ValidationOperator::NotBetween, "1", "10");
    CHECK(NumberPasses(notBetween, 0));
    CHECK(!NumberPasses(notBetween, 5));
}

static void TestDecimalOperators() {
    CHECK(NumberPasses(Rule(ValidationType::Decimal, ValidationOperator::GreaterThan, "0.5"), 0.75));
    CHECK(!NumberPasses(Rule(ValidationType::Decimal, ValidationOperator::GreaterThan, "0.5"), 0.5));
    CHECK(NumberPasses(Rule(ValidationType::Decimal, ValidationOperator::GreaterOrEqual, "0.5"), 0.5));
    CHECK(NumberPasses(Rule(ValidationType::Decimal, ValidationOperator::LessThan, "2.25"), 2.0));
    CHECK(NumberPasses(Rule(ValidationType::Decimal, ValidationOperator::LessOrEqual, "2.25"), 2.25));
    CHECK(NumberPasses(Rule(ValidationType::Decimal, ValidationOperator::Equal, "3.5"), 3.5));
    CHECK(!NumberPasses(Rule(ValidationType::Decimal, ValidationOperator::NotEqual, "3.5"), 3.5));
    // A bound that is not a number fails the check instead of reading as 0.
    CHECK(!NumberPasses(Rule(ValidationType::Decimal, ValidationOperator::GreaterThan, "abc"), 5));
    // Between with no upper bound cannot be checked.
    CHECK(!NumberPasses(Rule(ValidationType::Decimal, ValidationOperator::Between, "1"), 5));
}

static void TestDateAndTime() {
    const double jan15 = DateTimeValue::FromDate(2026, 1, 15).serialNumber;
    const auto in2026 = Rule(ValidationType::Date, ValidationOperator::Between, "2026-01-01", "2026-12-31");
    CHECK(NumberPasses(in2026, jan15));
    CHECK(!NumberPasses(in2026, DateTimeValue::FromDate(2025, 12, 31).serialNumber));
    // A serial number as the bound, as ODS and XLSX store it.
    char serial[32];
    std::snprintf(serial, sizeof serial, "%d", static_cast<int>(jan15));   // locale-ok: an integer
    CHECK(NumberPasses(Rule(ValidationType::Date, ValidationOperator::GreaterOrEqual, serial), jan15));

    const auto office = Rule(ValidationType::Time, ValidationOperator::Between, "09:00", "17:30:00");
    CHECK(NumberPasses(office, DateTimeValue::FromTime(12, 0, 0).serialNumber));
    CHECK(!NumberPasses(office, DateTimeValue::FromTime(18, 0, 0).serialNumber));
}

static void TestTextLength() {
    const auto upToFive = Rule(ValidationType::TextLength, ValidationOperator::LessOrEqual, "5");
    CHECK(TextPasses(upToFive, "Hello"));
    CHECK(!TextPasses(upToFive, "Hello!"));
    CHECK(TextPasses(upToFive, "Grüße"));   // 5 characters, 7 bytes
}

static void TestList() {
    const auto typed = Rule(ValidationType::List, ValidationOperator::Between, "Yes, No , Maybe");
    CHECK(TextPasses(typed, "Yes"));
    CHECK(TextPasses(typed, "No"));
    CHECK(TextPasses(typed, "Maybe"));
    CHECK(!TextPasses(typed, "yes"));
    CHECK(!TextPasses(typed, "Perhaps"));

    // ODF's quoted, semicolon-separated form.
    const auto odf = Rule(ValidationType::List, ValidationOperator::Between, "\"Red\";\"Green, light\"");
    CHECK(TextPasses(odf, "Red"));
    CHECK(TextPasses(odf, "Green, light"));
    CHECK(!TextPasses(odf, "Green"));
}

static void TestBlanksAndMessages() {
    auto rule = Rule(ValidationType::WholeNumber, ValidationOperator::Between, "1", "10");
    CHECK(CheckDataValidation(rule, nullptr));          // blank allowed by default
    rule.allowBlank = false;
    std::string message;
    CHECK(!CheckDataValidation(rule, nullptr, &message));
    CHECK(!message.empty());

    rule.allowBlank = true;
    rule.errorMessage = "Enter 1 to 10.";
    SpreadsheetCell cell;
    cell.SetNumber(42);
    cell.SetValidation(rule);
    message.clear();
    CHECK(!cell.ValidateValue(&message));
    CHECK(message == "Enter 1 to 10.");

    // Custom formulas are not evaluated and pass.
    CHECK(TextPasses(Rule(ValidationType::Custom, ValidationOperator::Between, "=A1>0"), "x"));
}

static void TestSheetRules() {
    SpreadsheetSheet sheet("Sheet1");
    sheet.GetCell(0, 0)->SetNumber(5);
    sheet.GetCell(1, 0)->SetNumber(50);
    sheet.GetCell(2, 0)->SetNumber(7);

    // A rule on A1:A2.
    sheet.SetValidation(CellRange(0, 0, 1, 0),
                        Rule(ValidationType::WholeNumber, ValidationOperator::LessOrEqual, "10"));
    CHECK(sheet.ValidateCell(0, 0));
    CHECK(!sheet.ValidateCell(1, 0));
    CHECK(sheet.ValidateCell(2, 0));   // outside the range, no rule of its own

    // A3 carries its own rule; with no range rule over it, that one applies.
    sheet.GetCell(2, 0)->SetValidation(Rule(ValidationType::WholeNumber, ValidationOperator::GreaterThan, "10"));
    CHECK(!sheet.ValidateCell(2, 0));
    // A range rule over it comes first.
    sheet.SetValidation(CellRange(2, 0, 2, 0),
                        Rule(ValidationType::WholeNumber, ValidationOperator::LessThan, "10"));
    CHECK(sheet.ValidateCell(2, 0));
}

int main() {
    TestNoRule();
    TestWholeNumber();
    TestDecimalOperators();
    TestDateAndTime();
    TestTextLength();
    TestList();
    TestBlanksAndMessages();
    TestSheetRules();

    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "SpreadsheetValidationTest: all checks passed\n";
    return 0;
}
