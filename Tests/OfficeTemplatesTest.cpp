// Tests/OfficeTemplatesTest.cpp
// The templates UltraOffice ships (media/templates/office/): made here, by the
// framework's own writers, and checked here.
//
//   OfficeTemplatesTest --write <dir>   writes the set into <dir>; run it with
//                                       media/templates/office after changing
//                                       a template below
//   OfficeTemplatesTest [<work dir>]    checks the shipped set: every template
//                                       is there, is a template (its media
//                                       type, IsOfficeTemplate - what makes
//                                       UltraOffice open it as a new, untitled
//                                       document), is of its kind, loads
//                                       through its kind's reader, and holds
//                                       what this file makes of it (so a
//                                       template changed here and not written
//                                       again fails)
//
// The set: documents (a letter, a report with headings, a memo), spreadsheets
// (a budget, an invoice, a simple list) and presentations (plain and dark,
// a title slide and a title and content slide each), in both OpenDocument and
// Office Open XML, so both kinds of reader and writer are used.
//
// Exit code is the number of failed checks.

#include "UltraCanvasFileLoader.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasPresentationOdp.h"
#include "UltraCanvasPresentationPptx.h"
#include "UltraCanvasRichDocument.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasSpreadsheetCell.h"
#include "UltraCanvasSpreadsheetSheet.h"
#include "UltraCanvasZipPackage.h"

#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;
namespace fs = std::filesystem;
using Kind = UltraCanvasFileLoader::OfficeDocumentKind;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s: %s\n", ok ? "  ok" : "FAIL", what.c_str());
}

// ===== DOCUMENTS =====

UCRichDocument Letter() {
    UCRichDocument doc = UCRichDocument::FromMarkdown(
        "Your Name  \nStreet and number  \nPostcode and town\n\n"
        "Recipient's name  \nStreet and number  \nPostcode and town\n\n"
        "Town, date\n\n"
        "**Subject of the letter**\n\n"
        "Dear Sir or Madam,\n\n"
        "The first paragraph says why you are writing.\n\n"
        "The second gives the details, and the last what you would like to happen next.\n\n"
        "Kind regards,\n\n\n"
        "Your Name\n");
    doc.metadata.title = "Letter";
    return doc;
}

UCRichDocument Report() {
    UCRichDocument doc = UCRichDocument::FromMarkdown(
        "# Report title\n\n"
        "*Author, date*\n\n"
        "## Summary\n\n"
        "What the report found, in a paragraph a reader in a hurry can stop after.\n\n"
        "## 1. Introduction\n\n"
        "Why the report was written and what it covers.\n\n"
        "## 2. Findings\n\n"
        "### 2.1 First finding\n\n"
        "What was found, and how.\n\n"
        "### 2.2 Second finding\n\n"
        "- A point\n- Another point\n\n"
        "## 3. Conclusion\n\n"
        "What follows from the findings, and what to do next.\n");
    doc.metadata.title = "Report";
    return doc;
}

UCRichDocument Memo() {
    UCRichDocument doc = UCRichDocument::FromMarkdown(
        "# Memo\n\n"
        "| | |\n|---|---|\n"
        "| **To:** | Name |\n"
        "| **From:** | Name |\n"
        "| **Date:** | Date |\n"
        "| **Subject:** | What it is about |\n\n"
        "---\n\n"
        "The message, short and to the point.\n\n"
        "- What has to be done\n- By whom, and by when\n");
    doc.metadata.title = "Memo";
    return doc;
}

// ===== SPREADSHEETS =====

void Style(UltraCanvasSpreadsheet& grid, int row, int col, const std::function<void(CellStyle&)>& change) {
    SpreadsheetCell* cell = grid.GetCell(row, col);
    if (!cell) return;
    CellStyle style = cell->GetStyle();
    change(style);
    cell->SetStyle(style);
}

void Heading(UltraCanvasSpreadsheet& grid, int row, int col, float size) {
    Style(grid, row, col, [size](CellStyle& s) { s.SetBold(); s.font.size = size; });
}

void HeaderRow(UltraCanvasSpreadsheet& grid, int row, const std::vector<std::string>& titles) {
    for (size_t c = 0; c < titles.size(); ++c) {
        const int col = static_cast<int>(c);
        grid.SetCellValue(row, col, titles[c]);
        Style(grid, row, col, [](CellStyle& s) {
            s.SetBold();
            s.SetBackgroundColor(Color(221, 230, 240, 255));
            CellBorder line;
            line.style = BorderStyle::Thin;
            line.color = Color(120, 130, 140, 255);
            s.borders.bottom = line;
        });
    }
}

void Money(UltraCanvasSpreadsheet& grid, int row, int col) {
    Style(grid, row, col, [](CellStyle& s) {
        s.numberFormat = NumberFormat::Number(2, true);
        s.hAlign = HorizontalAlignment::Right;   // a formula's result too
    });
}

std::shared_ptr<UltraCanvasSpreadsheet> NewGrid(const std::string& sheetName) {
    auto grid = std::make_shared<UltraCanvasSpreadsheet>("template", 0, 0, 800, 600);
    grid->RenameSheet(0, sheetName);
    return grid;
}

std::shared_ptr<UltraCanvasSpreadsheet> Budget() {
    auto grid = NewGrid("Budget");
    grid->SetCellValue(0, 0, std::string("Monthly budget"));
    Heading(*grid, 0, 0, 16);
    HeaderRow(*grid, 2, {"Category", "Planned", "Actual", "Difference"});
    const std::vector<std::pair<std::string, double>> lines = {
        {"Rent", 900}, {"Utilities", 150}, {"Groceries", 400}, {"Transport", 120}, {"Insurance", 80}, {"Leisure", 150}};
    int row = 3;
    for (const auto& [category, planned] : lines) {
        grid->SetCellValue(row, 0, category);
        grid->SetCellValue(row, 1, planned);
        const std::string r = std::to_string(row + 1);
        grid->SetCellFormula(row, 3, "=B" + r + "-C" + r);
        for (int c = 1; c <= 3; ++c) Money(*grid, row, c);
        ++row;
    }
    grid->SetCellValue(row, 0, std::string("Total"));
    Heading(*grid, row, 0, 11);
    const std::string last = std::to_string(row);
    for (int c = 1; c <= 3; ++c) {
        const std::string column(1, static_cast<char>('A' + c));
        grid->SetCellFormula(row, c, "=SUM(" + column + "4:" + column + last + ")");
        Money(*grid, row, c);
        Style(*grid, row, c, [](CellStyle& s) { s.SetBold(); });
    }
    grid->SetColumnWidth(0, 170);
    for (int c = 1; c <= 3; ++c) grid->SetColumnWidth(c, 110);
    grid->Recalculate();
    return grid;
}

std::shared_ptr<UltraCanvasSpreadsheet> Invoice() {
    auto grid = NewGrid("Invoice");
    grid->SetCellValue(0, 0, std::string("Invoice"));
    Heading(*grid, 0, 0, 20);
    const std::vector<std::pair<std::string, std::string>> fields = {
        {"From:", "Your company"}, {"To:", "Customer's name"}, {"Invoice number:", "0001"}, {"Date:", "Date"}};
    int row = 2;
    for (const auto& [caption, value] : fields) {
        grid->SetCellValue(row, 0, caption);
        Style(*grid, row, 0, [](CellStyle& s) { s.SetBold(); });
        grid->SetCellValue(row, 1, value);
        ++row;
    }
    HeaderRow(*grid, 7, {"Item", "Quantity", "Unit price", "Amount"});
    grid->SetCellValue(8, 0, std::string("Work done"));
    grid->SetCellValue(8, 1, 1.0);
    grid->SetCellValue(8, 2, 100.0);
    for (int r = 8; r <= 12; ++r) {
        const std::string n = std::to_string(r + 1);
        grid->SetCellFormula(r, 3, "=B" + n + "*C" + n);
        Money(*grid, r, 2);
        Money(*grid, r, 3);
    }
    grid->SetCellValue(14, 2, std::string("Subtotal"));
    grid->SetCellFormula(14, 3, "=SUM(D9:D13)");
    grid->SetCellValue(15, 2, std::string("Tax (20%)"));
    grid->SetCellFormula(15, 3, "=D15*0.2");
    grid->SetCellValue(16, 2, std::string("Total"));
    grid->SetCellFormula(16, 3, "=D15+D16");
    for (int r = 14; r <= 16; ++r) Money(*grid, r, 3);
    Style(*grid, 16, 2, [](CellStyle& s) { s.SetBold(); });
    Style(*grid, 16, 3, [](CellStyle& s) { s.SetBold(); });
    grid->SetColumnWidth(0, 200);
    for (int c = 1; c <= 3; ++c) grid->SetColumnWidth(c, 110);
    grid->Recalculate();
    return grid;
}

std::shared_ptr<UltraCanvasSpreadsheet> List() {
    auto grid = NewGrid("List");
    HeaderRow(*grid, 0, {"Item", "Who", "Due", "Done"});
    grid->SetCellValue(1, 0, std::string("First thing to do"));
    grid->SetColumnWidth(0, 240);
    grid->SetColumnWidth(1, 120);
    grid->SetColumnWidth(2, 100);
    grid->SetColumnWidth(3, 70);
    grid->FreezePanes(1, 0);
    return grid;
}

// ===== PRESENTATIONS =====

void SetText(PptxShape& shape, const std::vector<std::string>& lines) {
    if (shape.paragraphs.empty()) shape.paragraphs.emplace_back();
    PptxParagraph look = shape.paragraphs[0];
    if (look.runs.empty()) look.runs.emplace_back();
    shape.paragraphs.clear();
    for (const std::string& line : lines) {
        PptxParagraph p = look;
        p.runs.resize(1);
        p.runs[0].text = line;
        shape.paragraphs.push_back(p);
    }
}

// A title slide, as UltraOffice's new presentation starts: the title and,
// under it, the subtitle.
PptxSlide TitleSlide(const PptxPresentation& deck, const std::string& title, const std::string& subtitle) {
    PptxSlide slide = PptxSlideFromLayout(deck, -1);
    if (slide.shapes.size() >= 2) {
        PptxShape& t = slide.shapes[0];
        t.y = deck.slideHeight / 4;
        t.height = deck.slideHeight / 4;
        t.paragraphs[0].align = PptxAlign::Center;
        SetText(t, {title});
        PptxShape& s = slide.shapes[1];
        s.kind = PptxShapeKind::Subtitle;
        s.placeholder = "subTitle";
        s.y = deck.slideHeight / 2 + 914400 / 8;
        s.height = deck.slideHeight / 5;
        s.paragraphs[0].bullet = false;
        s.paragraphs[0].align = PptxAlign::Center;
        if (!s.paragraphs[0].runs.empty()) s.paragraphs[0].runs[0].sizePt = 24;
        SetText(s, {subtitle});
    }
    slide.title = title;
    return slide;
}

PptxSlide ContentSlide(const PptxPresentation& deck, const std::string& title, const std::vector<std::string>& points) {
    PptxSlide slide = PptxSlideFromLayout(deck, -1);
    if (slide.shapes.size() >= 2) {
        SetText(slide.shapes[0], {title});
        SetText(slide.shapes[1], points);
    }
    slide.title = title;
    return slide;
}

PptxPresentation PlainDeck() {
    PptxPresentation deck;
    deck.slideWidth = 12192000;   // 16:9
    deck.slideHeight = 6858000;
    deck.slides.push_back(TitleSlide(deck, "Presentation title", "Subtitle: your name and the date"));
    deck.slides.push_back(ContentSlide(deck, "Slide title", {"First point", "Second point", "Third point"}));
    for (size_t i = 0; i < deck.slides.size(); ++i) deck.slides[i].number = static_cast<int>(i) + 1;
    return deck;
}

// Light text on a dark slate background, the titles in a light blue.
constexpr const char* kDarkBackground = "1E2633";
constexpr const char* kDarkText = "F2F4F7";
constexpr const char* kDarkTitle = "8CC4FF";

void Darken(std::vector<PptxShape>& shapes) {
    for (PptxShape& shape : shapes) {
        const bool title = shape.kind == PptxShapeKind::Title;
        for (PptxParagraph& p : shape.paragraphs)
            for (PptxRun& run : p.runs) run.color = title ? kDarkTitle : kDarkText;
    }
}

// The plain deck, dark: its slides, and the design a new slide is made from
// - the masters and layouts the writer gives a deck, read back from it and
// darkened - so a slide added later is dark too.
bool DarkDeck(const fs::path& work, PptxPresentation& deck, std::string& error) {
    const std::string plain = PathToUtf8(work / "plain-for-dark.odp");
    if (!UltraCanvasFileLoader::SavePresentation(PlainDeck(), plain, error)) return false;
    if (!ReadOdpPresentation(plain, deck, error)) return false;
    for (PptxMaster& master : deck.masters) { master.background = kDarkBackground; Darken(master.placeholders); }
    for (PptxLayout& layout : deck.layouts) { layout.background = kDarkBackground; Darken(layout.placeholders); }
    for (PptxSlide& slide : deck.slides) {
        slide.background = kDarkBackground;
        slide.textColor = kDarkText;
        Darken(slide.shapes);
    }
    return true;
}

// ===== THE SET =====

struct Template {
    const char* file;
    Kind kind;
};

const std::vector<Template>& TheSet() {
    static const std::vector<Template> set = {
        {"letter.ott", Kind::Document},       {"report.dotx", Kind::Document},     {"memo.ott", Kind::Document},
        {"budget.xltx", Kind::Spreadsheet},   {"invoice.ots", Kind::Spreadsheet},  {"list.xltx", Kind::Spreadsheet},
        {"plain-deck.potx", Kind::Presentation}, {"dark-deck.otp", Kind::Presentation},
    };
    return set;
}

bool Write(const fs::path& dir, const fs::path& work, const std::string& file, std::string& error) {
    const std::string path = PathToUtf8(dir / PathFromUtf8(file));
    auto document = [&](const UCRichDocument& doc) { return UltraCanvasFileLoader::SaveTextDocument(doc, path, error); };
    auto sheet = [&](const std::shared_ptr<UltraCanvasSpreadsheet>& grid) {
        if (grid->SaveToFile(path)) return true;
        error = grid->GetLastError();
        return false;
    };
    if (file == "letter.ott") return document(Letter());
    if (file == "report.dotx") return document(Report());
    if (file == "memo.ott") return document(Memo());
    if (file == "budget.xltx") return sheet(Budget());
    if (file == "invoice.ots") return sheet(Invoice());
    if (file == "list.xltx") return sheet(List());
    if (file == "plain-deck.potx") return UltraCanvasFileLoader::SavePresentation(PlainDeck(), path, error);
    if (file == "dark-deck.otp") {
        PptxPresentation deck;
        return DarkDeck(work, deck, error) && UltraCanvasFileLoader::SavePresentation(deck, path, error);
    }
    error = "no such template";
    return false;
}

// ===== WHAT A TEMPLATE HOLDS =====
// A line per paragraph, cell or shape: enough to tell that a shipped
// template is the one this file makes, without comparing package bytes
// (which carry no content of their own to compare).

std::string Fingerprint(const std::string& path, Kind kind, std::string& error) {
    std::string out;
    if (kind == Kind::Document) {
        auto doc = UltraCanvasFileLoader::LoadTextDocument(path, error);
        return doc ? doc->ToPlainText() : std::string();
    }
    if (kind == Kind::Spreadsheet) {
        auto grid = std::make_shared<UltraCanvasSpreadsheet>("check", 0, 0, 800, 600);
        if (!grid->LoadFromFile(path)) { error = grid->GetLastError(); return {}; }
        for (int s = 0; s < grid->GetSheetCount(); ++s) {
            const SpreadsheetSheet* sheet = grid->GetSheet(s);
            if (!sheet) continue;
            out += "[" + sheet->GetName() + "]\n";
            const CellRange used = sheet->GetUsedRange();
            for (int r = used.start.row; r <= used.end.row; ++r)
                for (int c = used.start.col; c <= used.end.col; ++c) {
                    const SpreadsheetCell* cell = sheet->GetCellIfExists(r, c);
                    if (!cell) continue;
                    const std::string formula = cell->GetFormulaText();
                    const std::string value = formula.empty() ? cell->GetDisplayValue() : formula;
                    if (!value.empty()) out += std::to_string(r) + "," + std::to_string(c) + "=" + value + "\n";
                }
        }
        return out;
    }
    PptxPresentation deck;
    const bool read = IsOdpExtension(path) ? ReadOdpPresentation(path, deck, error) : ReadPptxPresentation(path, deck, error);
    if (!read) return {};
    for (const PptxSlide& slide : deck.slides) {
        out += "slide " + std::to_string(slide.number) + " bg=" + slide.background + "\n";
        for (const PptxShape& shape : slide.shapes)
            for (const PptxParagraph& p : shape.paragraphs) out += "  " + p.Text() + "\n";
    }
    return out;
}

bool HasTemplateMediaType(const std::string& path) {
    UCZipPackageReader zip;
    if (!zip.Open(path)) return false;
    std::string data;
    if (zip.ReadEntry("mimetype", data)) return data.find("-template") != std::string::npos;
    return zip.ReadEntry("[Content_Types].xml", data) && data.find("ml.template.main+xml") != std::string::npos;
}

int WriteSet(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path work = fs::temp_directory_path() / "uc-office-templates-write";
    fs::create_directories(work, ec);
    for (const Template& t : TheSet()) {
        std::string error;
        const bool ok = Write(dir, work, t.file, error);
        Check(ok, std::string("wrote ") + PathToUtf8(dir / PathFromUtf8(t.file)) + (error.empty() ? "" : ": " + error));
    }
    fs::remove_all(work, ec);
    return failures;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 2 && std::string(argv[1]) == "--write") {
        std::printf("=== Writing the office templates into %s ===\n", argv[2]);
        return WriteSet(PathFromUtf8(argv[2]));
    }

    std::printf("=== The shipped office templates ===\n");
#ifndef OFFICETEMPLATES_DIR
    Check(false, "OFFICETEMPLATES_DIR is not defined");
    return failures;
#else
    const fs::path shipped = PathFromUtf8(OFFICETEMPLATES_DIR);
    std::error_code ec;
    const fs::path work = argc > 1 ? PathFromUtf8(argv[1]) : fs::temp_directory_path() / "uc-office-templates-test";
    fs::remove_all(work, ec);
    const fs::path made = work / "made";
    fs::create_directories(made, ec);

    for (const Template& t : TheSet()) {
        const std::string path = PathToUtf8(shipped / PathFromUtf8(t.file));
        std::printf("\n-- %s --\n", t.file);
        if (!fs::exists(PathFromUtf8(path), ec)) {
            Check(false, path + " is shipped (run OfficeTemplatesTest --write media/templates/office)");
            continue;
        }
        Check(UltraCanvasFileLoader::IsOfficeTemplate(path), "a template: opened as a new, untitled document");
        Check(UltraCanvasFileLoader::DetectOfficeDocumentKind(path) == t.kind, "of its kind");
        Check(HasTemplateMediaType(path), "with the template's media type");

        std::string error;
        const std::string have = Fingerprint(path, t.kind, error);
        Check(!have.empty(), "loads through its kind's reader" + (error.empty() ? std::string() : ": " + error));
        std::string writeError;
        if (!Write(made, work, t.file, writeError)) {
            Check(false, "made again: " + writeError);
            continue;
        }
        const std::string want = Fingerprint(PathToUtf8(made / PathFromUtf8(t.file)), t.kind, error);
        Check(!want.empty() && have == want,
              "holds what this test makes of it (else run OfficeTemplatesTest --write media/templates/office)");
        if (have != want) std::printf("--- shipped:\n%s--- made:\n%s", have.c_str(), want.c_str());
    }

    // Nothing in the folder the set does not know: a template added by hand
    // would not be made again.
    size_t files = 0;
    for (const auto& entry : fs::directory_iterator(shipped, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        ++files;
        bool known = false;
        for (const Template& t : TheSet()) known = known || PathToUtf8(entry.path().filename()) == t.file;
        Check(known, PathToUtf8(entry.path().filename()) + " is made by this test");
    }
    Check(files == TheSet().size(), "the folder holds the set, no more");

    fs::remove_all(work, ec);
    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
#endif
}
