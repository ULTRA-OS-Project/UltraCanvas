// Tests/SpreadsheetRulesTest.cpp
// Conditional formats, validation rules, comments and links of a spreadsheet
// (UltraOffice phase 5, Docs/Research/UltraOfficeDesignProposal.md §5.2 items
// 4-6):
//  - what the conditional formats make of a cell (GetConditionalFormatting):
//    value comparisons, formula rules moved to each cell, text rules, top
//    values, duplicates, colour scales and data bars, and their figures
//    worked out again after a cell changes;
//  - validation: Custom (formula) rules, lists taken from cells, bounds that
//    are formulas;
//  - drawing: PaintCell, which the grid and the printed page share, fills a
//    cell by its rule, draws a data bar and marks a comment with a red corner;
//  - the grid's calls: comments and links as undo steps, Ctrl+click's
//    OpenHyperlink, the selection's validation and conditional format;
//  - all of it saved as .xlsx and .ods and loaded back, each written again
//    in the other format, and - where LibreOffice is installed (soffice) -
//    converted by it and loaded back.
//
// Usage: SpreadsheetRulesTest
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "IODeviceManager/UltraCanvasIODevicePrinterRasterTarget.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasSpreadsheet.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

using namespace UltraCanvas;
namespace fs = std::filesystem;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s: %s\n", ok ? "  ok" : "FAIL", what.c_str());
}

bool Near(const Color& a, const Color& b, int tolerance = 3) {
    return std::abs(a.r - b.r) <= tolerance && std::abs(a.g - b.g) <= tolerance && std::abs(a.b - b.b) <= tolerance;
}

std::string Rgb(const Color& c) {
    return std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b);
}

const Color kRedFill(255, 199, 206);
const Color kGreenFill(198, 239, 206);
const Color kBarColour(99, 142, 198);

CellStyle FillStyle(const Color& fill, const Color& text = Colors::Black, bool bold = false) {
    CellStyle style;
    style.fill = CellFill::Solid(fill);
    style.font.color = text;
    style.font.bold = bold;
    return style;
}

ConditionalFormat Format(const CellRange& range, const ConditionalFormatRule& rule) {
    ConditionalFormat format;
    format.range = range;
    format.rules.push_back(rule);
    return format;
}

// Sheet1 of the test workbook:
//   A1:A10   1..10, A1 > 7 red, A1 between 2 and 3 green
//   B1:B10   10..100, a three-colour scale
//   C1:C10   1..10, a data bar
//   D1:D5    text: apple, pear, apple, plum, grape; "contains ap" bold, duplicates red
//   E1:E10   1..10, a formula rule =E1>$F$1 (F1 = 8) green
//   G1:G3    lists: the items for a list rule
std::shared_ptr<UltraCanvasSpreadsheet> BuildWorkbook() {
    auto book = std::make_shared<UltraCanvasSpreadsheet>("rules", 0, 0, 900, 600);
    book->AddSheet("Other Sheet");
    SpreadsheetSheet* sheet = book->GetSheet(0);
    for (int r = 0; r < 10; ++r) {
        sheet->SetCellValue(r, 0, static_cast<double>(r + 1));
        sheet->SetCellValue(r, 1, static_cast<double>((r + 1) * 10));
        sheet->SetCellValue(r, 2, static_cast<double>(r + 1));
        sheet->SetCellValue(r, 4, static_cast<double>(r + 1));
    }
    const char* fruit[] = {"apple", "pear", "apple", "plum", "grape"};
    for (int r = 0; r < 5; ++r) sheet->SetCellValue(r, 3, std::string(fruit[r]));
    sheet->SetCellValue(0, 5, 8.0);
    sheet->SetCellValue(0, 6, std::string("North"));
    sheet->SetCellValue(1, 6, std::string("South"));
    sheet->SetCellValue(2, 6, std::string("East"));

    ConditionalFormatRule greater;
    greater.type = ConditionalFormatType::CellValue;
    greater.op = ValidationOperator::GreaterThan;
    greater.formula1 = "7";
    greater.style = FillStyle(kRedFill, Color(156, 0, 6));
    ConditionalFormatRule between;
    between.type = ConditionalFormatType::CellValue;
    between.op = ValidationOperator::Between;
    between.formula1 = "2";
    between.formula2 = "3";
    between.style = FillStyle(kGreenFill);
    ConditionalFormat first = Format(CellRange(0, 0, 9, 0), greater);
    first.rules.push_back(between);
    sheet->AddConditionalFormat(first);

    ConditionalFormatRule scale;
    scale.type = ConditionalFormatType::ColorScale;
    scale.colorScale = {Color(248, 105, 107), Color(255, 235, 132), Color(99, 190, 123)};
    sheet->AddConditionalFormat(Format(CellRange(0, 1, 9, 1), scale));

    ConditionalFormatRule bar;
    bar.type = ConditionalFormatType::DataBar;
    bar.dataBarColor = kBarColour;
    sheet->AddConditionalFormat(Format(CellRange(0, 2, 9, 2), bar));

    ConditionalFormatRule contains;
    contains.type = ConditionalFormatType::ContainsText;
    contains.formula1 = "ap";
    CellStyle bold;
    bold.font.bold = true;
    contains.style = bold;
    contains.priority = 2;
    ConditionalFormatRule duplicates;
    duplicates.type = ConditionalFormatType::DuplicateValues;
    duplicates.style = FillStyle(kRedFill);
    duplicates.priority = 1;
    ConditionalFormat textFormat = Format(CellRange(0, 3, 4, 3), contains);
    textFormat.rules.push_back(duplicates);
    sheet->AddConditionalFormat(textFormat);

    ConditionalFormatRule formula;
    formula.type = ConditionalFormatType::Expression;
    formula.formula1 = "=E1>$F$1";
    formula.style = FillStyle(kGreenFill);
    sheet->AddConditionalFormat(Format(CellRange(0, 4, 9, 4), formula));

    // Validations
    DataValidation whole;
    whole.type = ValidationType::WholeNumber;
    whole.op = ValidationOperator::Between;
    whole.formula1 = "1";
    whole.formula2 = "10";
    whole.alertStyle = ValidationAlertStyle::Warning;
    whole.errorTitle = "Out of range";
    whole.errorMessage = "Between 1 and 10, please";
    sheet->SetValidation(CellRange(0, 7, 4, 7), whole);             // H1:H5
    DataValidation list;
    list.type = ValidationType::List;
    list.formula1 = "Red,Green,Blue";
    sheet->SetValidation(CellRange(0, 8, 2, 8), list);              // I1:I3
    DataValidation fromCells;
    fromCells.type = ValidationType::List;
    fromCells.formula1 = "=$G$1:$G$3";
    sheet->SetValidation(CellRange(0, 9, 0, 9), fromCells);          // J1
    DataValidation custom;
    custom.type = ValidationType::Custom;
    custom.formula1 = "=K1>A1";
    custom.allowBlank = false;
    sheet->SetValidation(CellRange(0, 10, 4, 10), custom);           // K1:K5
    DataValidation length;
    length.type = ValidationType::TextLength;
    length.op = ValidationOperator::LessOrEqual;
    length.formula1 = "5";
    sheet->SetValidation(CellRange(0, 11, 0, 11), length);           // L1

    // Comments and links
    sheet->GetCell(0, 0)->SetComment(CellComment("The first value", "Ann"));
    sheet->GetCell(5, 12)->SetComment(CellComment("A note on an empty cell", "Bob"));   // M6
    sheet->SetCellValue(0, 13, std::string("Home page"));
    sheet->GetCell(0, 13)->SetHyperlink(CellHyperlink("https://example.com/a?b=1&c=2"));  // N1
    sheet->SetCellValue(1, 13, std::string("Mail us"));
    sheet->GetCell(1, 13)->SetHyperlink(CellHyperlink("mailto:someone@example.com", HyperlinkType::Email));
    sheet->SetCellValue(2, 13, std::string("Go there"));
    sheet->GetCell(2, 13)->SetHyperlink(CellHyperlink("'Other Sheet'.B4", HyperlinkType::InternalCell));
    book->Recalculate();
    return book;
}

const ConditionalFormatRule* FindRule(const SpreadsheetSheet& sheet, ConditionalFormatType type, int col) {
    for (const auto& format : sheet.GetConditionalFormats()) {
        if (format.range.start.col != col) continue;
        for (const auto& rule : format.rules) {
            if (rule.type == type) return &rule;
        }
    }
    return nullptr;
}

// What the rules make of the workbook's cells, as built or as loaded.
void CheckRules(const UltraCanvasSpreadsheet& book, const std::string& label, bool exactPriorities = true) {
    const SpreadsheetSheet* sheet = book.GetSheet(0);
    if (!sheet) {
        Check(false, label + ": a first sheet");
        return;
    }
    auto fillOf = [&](int row, int col) -> std::optional<Color> {
        const ConditionalCellFormat f = sheet->GetConditionalFormatting(row, col);
        if (f.fill) return f.fill;
        if (f.style && f.style->fill.HasFill()) return f.style->fill.foregroundColor;
        return std::nullopt;
    };
    // A: greater than 7 red, between 2 and 3 green
    auto a9 = fillOf(8, 0), a3 = fillOf(2, 0), a5 = fillOf(4, 0);
    Check(a9 && Near(*a9, kRedFill), label + ": A9 (9 > 7) red [" + (a9 ? Rgb(*a9) : "none") + "]");
    Check(a3 && Near(*a3, kGreenFill), label + ": A3 (3 between 2 and 3) green");
    Check(!a5, label + ": A5 (5) no rule holds");
    const ConditionalCellFormat a9format = sheet->GetConditionalFormatting(8, 0);
    Check(a9format.style && Near(a9format.style->font.color, Color(156, 0, 6)), label + ": A9's text dark red");
    // B: colour scale, lowest red, 50th percentile yellow, highest green
    auto b1 = fillOf(0, 1), b10 = fillOf(9, 1);
    Check(b1 && Near(*b1, Color(248, 105, 107)), label + ": B1 (lowest) red [" + (b1 ? Rgb(*b1) : "none") + "]");
    Check(b10 && Near(*b10, Color(99, 190, 123)), label + ": B10 (highest) green");
    auto b5 = fillOf(4, 1);
    Check(b5 && b5->r > 240 && b5->g > 200, label + ": B5 (near the middle) yellowish [" + (b5 ? Rgb(*b5) : "none") + "]");
    // C: data bar
    const ConditionalCellFormat c1 = sheet->GetConditionalFormatting(0, 2);
    const ConditionalCellFormat c10 = sheet->GetConditionalFormatting(9, 2);
    const ConditionalCellFormat c5 = sheet->GetConditionalFormatting(4, 2);
    Check(c1.hasDataBar && c1.dataBarLength < 0.01, label + ": C1's bar is the shortest");
    Check(c10.hasDataBar && c10.dataBarLength > 0.99 && Near(c10.dataBarColor, kBarColour),
          label + ": C10's bar is the longest, in the rule's colour");
    Check(c5.hasDataBar && c5.dataBarLength > 0.4 && c5.dataBarLength < 0.5, label + ": C5's bar 4/9 long");
    // D: duplicates (priority 1) red over "contains ap" bold
    auto d1 = fillOf(0, 3);
    Check(d1 && Near(*d1, kRedFill), label + ": D1 (apple, twice) a duplicate, red");
    const ConditionalCellFormat d5 = sheet->GetConditionalFormatting(4, 3);
    if (exactPriorities) Check(d5.style && d5.style->font.bold, label + ": D5 (grape) contains 'ap', bold");
    Check(!sheet->GetConditionalFormatting(1, 3).style, label + ": D2 (pear) neither");
    // E: =E1>$F$1 moved down the column; F1 = 8
    Check(fillOf(8, 4).has_value() && !fillOf(7, 4).has_value() && fillOf(9, 4).has_value(),
          label + ": E9, E10 over F1 green, E8 not");
}

// The validations, comments and links of the workbook.
void CheckAnnotations(const UltraCanvasSpreadsheet& book, const std::string& label, bool messages = true) {
    const SpreadsheetSheet* sheet = book.GetSheet(0);
    if (!sheet) return;
    const DataValidation* whole = sheet->GetEffectiveValidation(2, 7);
    Check(whole && whole->type == ValidationType::WholeNumber && whole->op == ValidationOperator::Between,
          label + ": H3 a whole number between ... rule");
    if (whole) {
        Check(whole->formula1 == "1" && whole->formula2 == "10",
              label + ": its bounds 1 and 10 [" + whole->formula1 + ", " + whole->formula2 + "]");
        Check(whole->alertStyle == ValidationAlertStyle::Warning, label + ": its alert a warning");
        if (messages) Check(whole->errorMessage == "Between 1 and 10, please", label + ": its message");
    }
    Check(!sheet->GetEffectiveValidation(5, 7), label + ": H6 has no rule");
    const auto items = sheet->GetValidationListItems(1, 8);
    Check(items.size() == 3 && items[0] == "Red" && items[2] == "Blue",
          label + ": I2's list Red, Green, Blue [" + std::to_string(items.size()) + "]");
    const auto cells = sheet->GetValidationListItems(0, 9);
    Check(cells.size() == 3 && cells[0] == "North" && cells[2] == "East",
          label + ": J1's list taken from G1:G3 [" + std::to_string(cells.size()) + "]");
    const DataValidation* custom = sheet->GetEffectiveValidation(3, 10);
    Check(custom && custom->type == ValidationType::Custom, label + ": K4 a custom rule");
    const DataValidation* length = sheet->GetEffectiveValidation(0, 11);
    Check(length && length->type == ValidationType::TextLength && length->op == ValidationOperator::LessOrEqual,
          label + ": L1 a text length rule");

    const SpreadsheetCell* a1 = sheet->GetCellIfExists(0, 0);
    Check(a1 && a1->HasComment() && a1->GetComment()->text == "The first value",
          label + ": A1's comment [" + (a1 && a1->HasComment() ? a1->GetComment()->text : "") + "]");
    Check(a1 && a1->HasComment() && a1->GetComment()->author == "Ann", label + ": by Ann");
    const SpreadsheetCell* m6 = sheet->GetCellIfExists(5, 12);
    Check(m6 && m6->HasComment() && m6->GetComment()->text == "A note on an empty cell",
          label + ": the comment on the empty M6");
    const SpreadsheetCell* n1 = sheet->GetCellIfExists(0, 13);
    Check(n1 && n1->HasHyperlink() && n1->GetHyperlink()->target == "https://example.com/a?b=1&c=2" &&
              n1->GetDisplayValue() == "Home page",
          label + ": N1 links to the web page [" + (n1 && n1->HasHyperlink() ? n1->GetHyperlink()->target : "") + "]");
    const SpreadsheetCell* n2 = sheet->GetCellIfExists(1, 13);
    Check(n2 && n2->HasHyperlink() && n2->GetHyperlink()->target == "mailto:someone@example.com",
          label + ": N2 a mail link");
    const SpreadsheetCell* n3 = sheet->GetCellIfExists(2, 13);
    Check(n3 && n3->HasHyperlink() && n3->GetHyperlink()->type == HyperlinkType::InternalCell &&
              n3->GetHyperlink()->target.find("B4") != std::string::npos,
          label + ": N3 links into the workbook [" + (n3 && n3->HasHyperlink() ? n3->GetHyperlink()->target : "") + "]");
}

void CheckEvaluation() {
    std::printf("\n--- rules evaluated ---\n");
    auto book = BuildWorkbook();
    CheckRules(*book, "built");
    SpreadsheetSheet* sheet = book->GetActiveSheet();

    // A changed value is drawn by the figures as they are now.
    sheet->SetCellValue(9, 1, 1000.0);
    auto b9 = sheet->GetConditionalFormatting(8, 1).fill;
    Check(b9 && b9->r > 240, "B10 raised to 1000: B9 is now near the low end, reddish-yellow");
    sheet->SetCellValue(9, 1, 100.0);

    ConditionalFormatRule top;
    top.type = ConditionalFormatType::Top10;
    top.formula1 = "2";
    top.style = FillStyle(kGreenFill);
    sheet->AddConditionalFormat(Format(CellRange(0, 0, 9, 0), top));
    // A10 (10) is over 7 too: the earlier rule's red comes first.
    Check(sheet->GetConditionalFormatting(8, 0).style.has_value(), "A9 keeps the first rule that holds");
    sheet->RemoveConditionalFormat(static_cast<int>(sheet->GetConditionalFormats().size()) - 1);

    // Custom rule: K1 > A1 (moved to each row)
    sheet->SetCellValue(0, 10, 5.0);   // 5 > 1
    sheet->SetCellValue(1, 10, 1.0);   // 1 > 2 is false
    std::string message;
    Check(sheet->ValidateCell(0, 10, &message), "K1 = 5 passes =K1>A1");
    Check(!sheet->ValidateCell(1, 10, &message), "K2 = 1 fails =K2>A2");
    Check(!sheet->ValidateCell(4, 10, &message), "K5 empty fails (blanks not allowed)");
    // A list taken from cells
    sheet->SetCellValue(0, 9, std::string("South"));
    Check(sheet->ValidateCell(0, 9), "J1 = South is in G1:G3");
    sheet->SetCellValue(0, 9, std::string("West"));
    Check(!sheet->ValidateCell(0, 9), "J1 = West is not");
    // A bound that is a formula
    DataValidation upTo;
    upTo.type = ValidationType::Decimal;
    upTo.op = ValidationOperator::LessOrEqual;
    upTo.formula1 = "=$F$1";
    sheet->SetValidation(CellRange(0, 14, 0, 14), upTo);   // O1 <= F1 (8)
    sheet->SetCellValue(0, 14, 7.5);
    Check(sheet->ValidateCell(0, 14), "O1 = 7.5 <= F1");
    sheet->SetCellValue(0, 14, 9.0);
    Check(!sheet->ValidateCell(0, 14), "O1 = 9 is not <= F1");
    // The grid offers the list taken from cells
    const auto offered = book->GetValidationListItems(0, 9);
    Check(offered.size() == 3, "the grid's list arrow offers G1:G3");
}

// PaintCell on an off-screen page: the colours a rule gives, a data bar, a
// comment's red corner.
void CheckDrawing() {
    std::printf("\n--- drawn ---\n");
    auto book = BuildWorkbook();
    const SpreadsheetSheet& sheet = *book->GetSheet(0);
    RasterPageTarget page(400, 100, 72, 72);
    if (!page.IsValid()) {
        std::printf("  skipped: no off-screen surface\n");
        return;
    }
    page.BeginPage();
    IRenderContext* ctx = page.GetRenderContext();
    // Four cells, 80 x 20, side by side: A9 (red rule), C10 (full bar),
    // C1 (shortest bar), A1 (a comment).
    UltraCanvasSpreadsheet::PaintCell(ctx, sheet, 8, 0, Rect2Di(0, 0, 80, 20));
    UltraCanvasSpreadsheet::PaintCell(ctx, sheet, 9, 2, Rect2Di(100, 0, 80, 20));
    UltraCanvasSpreadsheet::PaintCell(ctx, sheet, 0, 2, Rect2Di(200, 0, 80, 20));
    UltraCanvasSpreadsheet::PaintCell(ctx, sheet, 0, 0, Rect2Di(300, 0, 80, 20));
    std::vector<std::vector<uint8_t>> rows;
    page.ForEachRow([&rows](const uint8_t* rgba, int width) {
        rows.emplace_back(rgba, rgba + static_cast<size_t>(width) * 4u);
        return true;
    });
    auto pixel = [&rows](int x, int y) {
        const uint8_t* p = rows[static_cast<size_t>(y)].data() + static_cast<size_t>(x) * 4u;
        return Color(p[0], p[1], p[2]);
    };
    if (rows.size() < 20) {
        Check(false, "the page reads back");
        return;
    }
    Check(Near(pixel(5, 10), kRedFill, 6), "A9 filled red by its rule [" + Rgb(pixel(5, 10)) + "]");
    // The bar is drawn translucent over white: bluish.
    const Color bar = pixel(110, 4);
    Check(bar.b > bar.r + 30, "C10's bar reaches across the cell [" + Rgb(bar) + "]");
    const Color noBar = pixel(230, 4);
    Check(noBar.r > 240 && noBar.g > 240 && noBar.b > 240, "C1's bar does not [" + Rgb(noBar) + "]");
    const Color corner = pixel(378, 1);
    Check(corner.r > 180 && corner.g < 90, "A1's comment marked by a red corner [" + Rgb(corner) + "]");
    const Color plain = pixel(305, 15);
    Check(plain.r > 240 && plain.g > 240, "A1 itself has no fill [" + Rgb(plain) + "]");
}

void CheckGridCalls() {
    std::printf("\n--- the grid's calls ---\n");
    auto book = BuildWorkbook();
    SpreadsheetSheet* sheet = book->GetActiveSheet();
    book->SetCellComment(3, 3, "Ripe?", "Cy");
    Check(sheet->GetCellIfExists(3, 3)->HasComment(), "SetCellComment puts a comment on D4");
    book->Undo();
    Check(!sheet->GetCellIfExists(3, 3)->HasComment(), "undo takes it away");
    book->Redo();
    Check(sheet->GetCellIfExists(3, 3)->HasComment(), "redo puts it back");
    book->RemoveCellComment(3, 3);
    Check(!sheet->GetCellIfExists(3, 3)->HasComment(), "RemoveCellComment");

    CellHyperlink link("https://ultra.example/");
    book->SetCellHyperlink(20, 0, link);
    const SpreadsheetCell* a21 = sheet->GetCellIfExists(20, 0);
    Check(a21 && a21->HasHyperlink() && a21->GetDisplayValue() == "https://ultra.example/",
          "a link on an empty cell shows its address");
    std::string opened;
    book->onOpenHyperlink = [&opened](const CellHyperlink& l) { opened = l.target; };
    Check(book->OpenHyperlink(20, 0) && opened == "https://ultra.example/", "OpenHyperlink hands the address on");
    Check(!book->OpenHyperlink(21, 0), "a cell without a link opens nothing");
    book->onOpenHyperlink = nullptr;
    Check(book->OpenHyperlink(2, 13) && book->GetActiveSheetIndex() == 1 &&
              book->GetActiveCell().row == 3 && book->GetActiveCell().col == 1,
          "a link into the workbook goes to 'Other Sheet'.B4");
    book->SetActiveSheet(0);

    book->Select(CellRange(30, 0, 32, 0));
    DataValidation positive;
    positive.type = ValidationType::Decimal;
    positive.op = ValidationOperator::GreaterThan;
    positive.formula1 = "0";
    book->SetSelectionValidation(positive);
    Check(sheet->GetEffectiveValidation(31, 0) && sheet->GetEffectiveValidation(31, 0)->type == ValidationType::Decimal,
          "SetSelectionValidation sets A31:A33's rule");
    book->SetSelectionValidation(DataValidation());
    Check(!sheet->GetEffectiveValidation(31, 0), "a rule of type Any clears it");
    ConditionalFormatRule blank;
    blank.type = ConditionalFormatType::ContainsBlanks;
    blank.style = FillStyle(kRedFill);
    const size_t before = sheet->GetConditionalFormats().size();
    book->AddSelectionConditionalFormat(blank);
    Check(sheet->GetConditionalFormats().size() == before + 1 &&
              sheet->GetConditionalFormatting(31, 0).style.has_value(),
          "AddSelectionConditionalFormat: the blank A32 is filled");
    book->ClearSelectionConditionalFormats();
    Check(sheet->GetConditionalFormats().size() == before, "ClearSelectionConditionalFormats");
}

std::shared_ptr<UltraCanvasSpreadsheet> Load(const std::string& path) {
    auto book = std::make_shared<UltraCanvasSpreadsheet>("loaded", 0, 0, 900, 600);
    if (!book->LoadFromFile(path)) {
        std::printf("      cannot load %s: %s\n", path.c_str(), book->GetLastError().c_str());
        return nullptr;
    }
    return book;
}

bool HaveSoffice() {
#ifdef _WIN32
    return false;
#else
    return std::system("command -v soffice > /dev/null 2>&1") == 0;
#endif
}

std::string Convert(const fs::path& source, const std::string& ext, const fs::path& outDir, const fs::path& profile) {
    std::error_code ec;
    fs::create_directories(outDir, ec);
    const std::string command = "soffice -env:UserInstallation=file://" + PathToUtf8(profile) +
                                " --headless --convert-to " + ext + " --outdir '" + PathToUtf8(outDir) + "' '" +
                                PathToUtf8(source) + "' > /dev/null 2>&1";
    if (std::system(command.c_str()) != 0) return "";
    const fs::path result = outDir / PathFromUtf8(PathToUtf8(source.stem()) + "." + ext);
    return fs::exists(result, ec) ? PathToUtf8(result) : std::string();
}

// What another program's conversion of our file keeps: the value and
// formula rules, the scale, the bar, the lists, the comments, the web link.
void CheckConverted(const UltraCanvasSpreadsheet& book, const std::string& label) {
    CheckRules(book, label, false);
    const SpreadsheetSheet* sheet = book.GetSheet(0);
    if (!sheet) return;
    const auto items = sheet->GetValidationListItems(1, 8);
    Check(items.size() == 3 && items[1] == "Green", label + ": I2's list");
    const auto cells = sheet->GetValidationListItems(0, 9);
    Check(cells.size() == 3 && cells[1] == "South", label + ": J1's list from G1:G3");
    const DataValidation* whole = sheet->GetEffectiveValidation(2, 7);
    Check(whole && whole->type == ValidationType::WholeNumber, label + ": H3's whole number rule");
    const SpreadsheetCell* a1 = sheet->GetCellIfExists(0, 0);
    Check(a1 && a1->HasComment() && a1->GetComment()->text.find("The first value") != std::string::npos,
          label + ": A1's comment");
    const SpreadsheetCell* n1 = sheet->GetCellIfExists(0, 13);
    Check(n1 && n1->HasHyperlink() && n1->GetHyperlink()->target == "https://example.com/a?b=1&c=2",
          label + ": N1's link");
}

void CheckFiles() {
    std::printf("\n--- saved and loaded ---\n");
    std::error_code ec;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() / PathFromUtf8("uc-rules-roundtrip-" + std::to_string(stamp));
    fs::create_directories(dir, ec);

    auto book = BuildWorkbook();
    const fs::path xlsx = dir / "rules.xlsx";
    const fs::path ods = dir / "rules.ods";
    Check(book->SaveToFile(PathToUtf8(xlsx)), "saved as .xlsx " + book->GetLastError());
    Check(book->SaveToFile(PathToUtf8(ods)), "saved as .ods " + book->GetLastError());
    for (const auto& [path, name] : {std::make_pair(xlsx, std::string(".xlsx")), std::make_pair(ods, std::string(".ods"))}) {
        if (auto loaded = Load(PathToUtf8(path))) {
            CheckRules(*loaded, name + " loaded");
            CheckAnnotations(*loaded, name + " loaded");
            const fs::path again = dir / (name == ".xlsx" ? "again.ods" : "again.xlsx");
            Check(loaded->SaveToFile(PathToUtf8(again)), name + " loaded, saved in the other format");
            if (auto twice = Load(PathToUtf8(again))) {
                CheckRules(*twice, name + " -> " + PathToUtf8(again.extension()));
                CheckAnnotations(*twice, name + " -> " + PathToUtf8(again.extension()));
            } else {
                Check(false, name + " saved again loads");
            }
        } else {
            Check(false, name + " loads");
        }
    }

    std::printf("\n--- LibreOffice reads them ---\n");
    if (!HaveSoffice()) {
        std::printf("  skipped: soffice is not installed\n");
    } else {
        const fs::path profile = dir / "lo-profile";
        const std::string fromXlsx = Convert(xlsx, "ods", dir / "lo-from-xlsx", profile);
        const std::string fromOds = Convert(ods, "xlsx", dir / "lo-from-ods", profile);
        if (fromXlsx.empty()) std::printf("  skipped: soffice did not convert the .xlsx\n");
        else if (auto loaded = Load(fromXlsx)) CheckConverted(*loaded, "our .xlsx as LibreOffice's .ods");
        else Check(false, "LibreOffice's .ods loads");
        if (fromOds.empty()) std::printf("  skipped: soffice did not convert the .ods\n");
        else if (auto loaded = Load(fromOds)) CheckConverted(*loaded, "our .ods as LibreOffice's .xlsx");
        else Check(false, "LibreOffice's .xlsx loads");
    }

    if (!GetEnvUtf8("UC_KEEP_ROUNDTRIP_FILES").empty()) {
        std::printf("\nfiles kept in %s\n", PathToUtf8(dir).c_str());
    } else {
        fs::remove_all(dir, ec);
    }
}

} // namespace

int main() {
    std::printf("=== Spreadsheet: conditional formats, validation, comments and links ===\n");
    CheckEvaluation();
    CheckDrawing();
    CheckGridCalls();
    CheckFiles();
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
