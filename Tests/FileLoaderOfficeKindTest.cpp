// Tests/FileLoaderOfficeKindTest.cpp
// What an office suite opens a file as (UltraCanvasFileLoader::
// DetectOfficeDocumentKind): by its extension, and by what its package holds
// when the name says nothing (an OpenDocument mimetype, an Office Open XML
// main part). And the Open dialog's types for the three kinds
// (AddOfficeDocumentLoadTypes), where a deck is a presentation, not a
// document as AddTextDocumentLoadTypes offers it. And the templates of the
// three kinds (.ott / .dotx, .ots / .xltx, .otp / .potx): their kind, that
// they are templates (IsOfficeTemplate, which an office suite opens as a new,
// untitled document), "Save as template"'s types, and each written by its
// kind's writer with the template's media type and read back.
//
// Usage: FileLoaderOfficeKindTest
// Exit code is the number of failed checks.

#include "UltraCanvasFileLoader.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasPresentationOdp.h"
#include "UltraCanvasPresentationPptx.h"
#include "UltraCanvasRichDocument.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasZipPackage.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

using namespace UltraCanvas;
using Kind = UltraCanvasFileLoader::OfficeDocumentKind;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s: %s\n", ok ? "  ok" : "FAIL", what.c_str());
}

const char* Name(Kind kind) {
    switch (kind) {
        case Kind::Document: return "Document";
        case Kind::Spreadsheet: return "Spreadsheet";
        case Kind::Presentation: return "Presentation";
        default: return "Unknown";
    }
}

void Expect(const std::string& path, Kind want, const std::string& what) {
    const Kind got = UltraCanvasFileLoader::DetectOfficeDocumentKind(path);
    Check(got == want, what + " [" + Name(got) + "]");
}

// A copy of `source` with no extension, so only its content can tell.
std::string WithoutExtension(const std::filesystem::path& dir, const std::string& source, const std::string& name) {
    const std::filesystem::path target = dir / PathFromUtf8(name);
    std::error_code ec;
    std::filesystem::copy_file(PathFromUtf8(source), target, std::filesystem::copy_options::overwrite_existing, ec);
    return PathToUtf8(target);
}

bool Holds(const FileFilter* filter, const std::string& ext) {
    return filter && std::find(filter->extensions.begin(), filter->extensions.end(), ext) != filter->extensions.end();
}

bool AnyHolds(const FileDialogOptions& opts, const std::string& ext) {
    for (const auto& filter : opts.filters) if (Holds(&filter, ext)) return true;
    return false;
}

std::string Entry(const std::string& path, const char* name) {
    UCZipPackageReader zip;
    std::string data;
    if (zip.Open(path)) zip.ReadEntry(name, data);
    return data;
}

bool Contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

} // namespace

int main() {
    std::printf("=== FileLoader: office document kinds ===\n");

    std::printf("\n-- by extension --\n");
    Expect("report.docx", Kind::Document, ".docx is a document");
    Expect("letter.ODT", Kind::Document, ".ODT, any case");
    Expect("old.doc", Kind::Document, ".doc");
    Expect("notes.md", Kind::Document, ".md");
    Expect("budget.xlsx", Kind::Spreadsheet, ".xlsx is a spreadsheet");
    Expect("budget.ods", Kind::Spreadsheet, ".ods");
    Expect("legacy.xls", Kind::Spreadsheet, ".xls");
    Expect("export.csv", Kind::Spreadsheet, ".csv");
    Expect("deck.pptx", Kind::Presentation, ".pptx is a presentation, not an outline document");
    Expect("deck.potx", Kind::Presentation, ".potx");
    Expect("deck.odp", Kind::Presentation, ".odp");
    Expect("photo.png", Kind::Unknown, ".png is none of them");

    std::printf("\n-- by content, without an extension --\n");
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "uc-office-kind-test";
    std::filesystem::create_directories(dir, ec);
#ifdef OFFICETEST_FIXTURE_DIR
    const std::string fixtures = OFFICETEST_FIXTURE_DIR;
    Expect(WithoutExtension(dir, fixtures + "/word-frames.odt", "odt-file"), Kind::Document, "an OpenDocument text");
    Expect(WithoutExtension(dir, fixtures + "/word-frames.docx", "docx-file"), Kind::Document, "a Word package");
    Expect(WithoutExtension(dir, fixtures + "/pptx-features.pptx", "pptx-file"), Kind::Presentation, "a PowerPoint package");
    Expect(WithoutExtension(dir, fixtures + "/pptx-features.odp", "odp-file"), Kind::Presentation, "an OpenDocument presentation");
#else
    Check(false, "OFFICETEST_FIXTURE_DIR is not defined");
#endif
    {
        auto sheet = std::make_shared<UltraCanvasSpreadsheet>("sheet", 0, 0, 400, 300);
        sheet->SetCellValue(0, 0, 42.0);
        const std::string ods = PathToUtf8(dir / "made.ods");
        const std::string xlsx = PathToUtf8(dir / "made.xlsx");
        Check(sheet->SaveToFile(ods) && sheet->SaveToFile(xlsx), "a spreadsheet saved as .ods and .xlsx");
        Expect(WithoutExtension(dir, ods, "ods-file"), Kind::Spreadsheet, "an OpenDocument spreadsheet");
        Expect(WithoutExtension(dir, xlsx, "xlsx-file"), Kind::Spreadsheet, "an Excel package");
    }
    {
        const std::string text = PathToUtf8(dir / "plain-file");
        if (FILE* f = OpenFileUtf8(text, "wb")) { std::fputs("just text\n", f); std::fclose(f); }
        Expect(text, Kind::Unknown, "plain text without an extension is none of them");
    }
    Expect(PathToUtf8(dir / "does-not-exist"), Kind::Unknown, "a missing file");

    std::printf("\n-- the Open dialog's types --\n");
    FileDialogOptions open;
    UltraCanvasFileLoader::AddOfficeDocumentLoadTypes(open);
    Check(!open.filters.empty() && Holds(&open.filters.front(), "docx") && Holds(&open.filters.front(), "xlsx") &&
          Holds(&open.filters.front(), "pptx"), "the first filter holds all three kinds");
    const FileFilter* documents = nullptr;
    const FileFilter* sheets = nullptr;
    const FileFilter* decks = nullptr;
    for (const auto& filter : open.filters) {
        if (Holds(&filter, "odt") && !documents && &filter != &open.filters.front()) documents = &filter;
        if (Holds(&filter, "ods") && !sheets && &filter != &open.filters.front()) sheets = &filter;
        if (Holds(&filter, "odp") && !decks && &filter != &open.filters.front()) decks = &filter;
    }
    Check(documents && !Holds(documents, "pptx") && !Holds(documents, "xlsx"), "Documents holds no deck or sheet");
    Check(sheets && Holds(sheets, "xlsx") && !Holds(sheets, "docx"), "Spreadsheets holds .xlsx");
    Check(decks && Holds(decks, "pptx") && !Holds(decks, "docx"), "Presentations holds .pptx and .odp");

    std::printf("\n-- the Save dialog's types for a deck --\n");
    FileDialogOptions save;
    save.defaultFileName = "talk.odp";
    UltraCanvasFileLoader::AddPresentationSaveTypes(save);
    Check(!save.filters.empty() && Holds(&save.filters.front(), "odp"), "the type of the proposed name comes first");
    bool anyPptx = false;
    for (const auto& filter : save.filters) anyPptx = anyPptx || Holds(&filter, "pptx");
    Check(anyPptx, "PowerPoint is offered");

    std::printf("\n-- templates --\n");
    Expect("letter.ott", Kind::Document, ".ott is a document");
    Expect("letter.DOTX", Kind::Document, ".dotx");
    Expect("letter.dotm", Kind::Document, ".dotm");
    Expect("budget.ots", Kind::Spreadsheet, ".ots is a spreadsheet");
    Expect("budget.xltx", Kind::Spreadsheet, ".xltx");
    Expect("budget.xltm", Kind::Spreadsheet, ".xltm");
    Expect("deck.otp", Kind::Presentation, ".otp is a presentation");
    for (const char* name : {"a.ott", "a.dotx", "a.dotm", "a.ots", "a.xltx", "a.xltm", "a.otp", "a.potx", "a.potm"})
        Check(UltraCanvasFileLoader::IsOfficeTemplate(name) && UltraCanvasFileLoader::IsOfficeTemplateExtension(name),
              std::string(name) + " is a template");
    for (const char* name : {"a.odt", "a.docx", "a.ods", "a.xlsx", "a.odp", "a.pptx", "a.csv", "a.md"})
        Check(!UltraCanvasFileLoader::IsOfficeTemplate(name), std::string(name) + " is no template");

    // Each kind's writer writes its templates with the template's media
    // type, and they read back.
    const UCRichDocument letter = UCRichDocument::FromMarkdown("# Letter\n\nDear reader,\n");
    for (const char* name : {"made.ott", "made.dotx"}) {
        const std::string path = PathToUtf8(dir / name);
        std::string error;
        Check(UltraCanvasFileLoader::SaveTextDocument(letter, path, error), std::string("SaveTextDocument writes ") + name);
        auto back = UltraCanvasFileLoader::LoadTextDocument(path, error);
        Check(back && Contains(back->ToPlainText(), "Dear reader,"), "... and reads it back");
    }
    Check(Entry(PathToUtf8(dir / "made.ott"), "mimetype") == "application/vnd.oasis.opendocument.text-template",
          ".ott: the text template's mimetype");
    Check(Contains(Entry(PathToUtf8(dir / "made.dotx"), "[Content_Types].xml"), "wordprocessingml.template.main+xml"),
          ".dotx: the Word template's content type");
    {
        auto sheet = std::make_shared<UltraCanvasSpreadsheet>("template-sheet", 0, 0, 400, 300);
        sheet->SetCellValue(0, 0, std::string("Budget"));
        sheet->SetCellValue(1, 0, 12.5);
        for (const char* name : {"made.ots", "made.xltx"}) {
            const std::string path = PathToUtf8(dir / name);
            Check(sheet->SaveToFile(path), std::string("SaveToFile writes ") + name);
            auto back = std::make_shared<UltraCanvasSpreadsheet>("template-back", 0, 0, 400, 300);
            Check(back->LoadFromFile(path) && back->GetCellText(0, 0) == "Budget" && back->GetCellNumber(1, 0) == 12.5,
                  "... and LoadFromFile reads it back");
        }
        Check(Entry(PathToUtf8(dir / "made.ots"), "mimetype") == "application/vnd.oasis.opendocument.spreadsheet-template",
              ".ots: the spreadsheet template's mimetype");
        Check(Contains(Entry(PathToUtf8(dir / "made.ots"), "META-INF/manifest.xml"), "spreadsheet-template"),
              "... in its manifest too");
        const std::string types = Entry(PathToUtf8(dir / "made.xltx"), "[Content_Types].xml");
        Check(Contains(types, "spreadsheetml.template.main+xml") && !Contains(types, "spreadsheetml.sheet.main+xml"),
              ".xltx: the Excel template's content type");
        Check(Entry(PathToUtf8(dir / "made.ods"), "mimetype") == "application/vnd.oasis.opendocument.spreadsheet",
              "an .ods keeps the spreadsheet's mimetype");
        Check(!sheet->SaveToFile(PathToUtf8(dir / "made.xltm")) && !std::filesystem::exists(dir / "made.xltm"),
              "an .xltm is read, not written");
        std::filesystem::copy_file(dir / "made.xltx", dir / "macros.xltm", std::filesystem::copy_options::overwrite_existing, ec);
        auto macros = std::make_shared<UltraCanvasSpreadsheet>("template-xltm", 0, 0, 400, 300);
        Check(macros->LoadFromFile(PathToUtf8(dir / "macros.xltm")) && macros->GetCellText(0, 0) == "Budget",
              "... and read");
    }
    {
        PptxPresentation deck;
        deck.slideWidth = 12192000;
        deck.slideHeight = 6858000;
        PptxSlide slide = PptxSlideFromLayout(deck, -1);
        if (!slide.shapes.empty() && !slide.shapes[0].paragraphs.empty() && !slide.shapes[0].paragraphs[0].runs.empty())
            slide.shapes[0].paragraphs[0].runs[0].text = "Template title";
        slide.number = 1;
        deck.slides.push_back(slide);
        for (const char* name : {"made.potx", "made.otp"}) {
            const std::string path = PathToUtf8(dir / name);
            std::string error;
            Check(UltraCanvasFileLoader::SavePresentation(deck, path, error), std::string("SavePresentation writes ") + name);
            PptxPresentation back;
            const bool read = IsOdpExtension(path) ? ReadOdpPresentation(path, back, error) : ReadPptxPresentation(path, back, error);
            Check(read && back.slides.size() == 1 && back.slides[0].title == "Template title", "... and reads it back");
        }
        Check(Contains(Entry(PathToUtf8(dir / "made.potx"), "[Content_Types].xml"), "presentationml.template.main+xml"),
              ".potx: the PowerPoint template's content type");
        Check(Entry(PathToUtf8(dir / "made.otp"), "mimetype") == "application/vnd.oasis.opendocument.presentation-template",
              ".otp: the presentation template's mimetype");
    }
    // Without an extension, the package tells a template from a document.
    for (const char* made : {"made.ott", "made.dotx", "made.ots", "made.xltx", "made.potx", "made.otp"}) {
        const std::string bare = WithoutExtension(dir, PathToUtf8(dir / made), std::string(made).substr(5) + "-template");
        Check(UltraCanvasFileLoader::IsOfficeTemplate(bare), std::string("a ") + made + " without its extension is a template");
    }
    Expect(WithoutExtension(dir, PathToUtf8(dir / "made.ott"), "ott-kind"), Kind::Document, "... an .ott a document");
    Expect(WithoutExtension(dir, PathToUtf8(dir / "made.xltx"), "xltx-kind"), Kind::Spreadsheet, "... an .xltx a spreadsheet");
    Expect(WithoutExtension(dir, PathToUtf8(dir / "made.otp"), "otp-kind"), Kind::Presentation, "... an .otp a presentation");
    Check(!UltraCanvasFileLoader::IsOfficeTemplate(WithoutExtension(dir, PathToUtf8(dir / "made.ods"), "ods-plain")),
          "... and an .ods without its extension is none");

    std::printf("\n-- the Save dialog's types: templates apart --\n");
    FileDialogOptions documentTemplates, sheetTemplates, deckTemplates;
    UltraCanvasFileLoader::AddOfficeTemplateSaveTypes(documentTemplates, Kind::Document);
    UltraCanvasFileLoader::AddOfficeTemplateSaveTypes(sheetTemplates, Kind::Spreadsheet);
    UltraCanvasFileLoader::AddOfficeTemplateSaveTypes(deckTemplates, Kind::Presentation);
    Check(AnyHolds(documentTemplates, "ott") && AnyHolds(documentTemplates, "dotx") && !AnyHolds(documentTemplates, "dotm") &&
              !AnyHolds(documentTemplates, "docx") && !AnyHolds(documentTemplates, "potx"),
          "Save as template, a document: .ott and .dotx (not .dotm, which is read only)");
    Check(AnyHolds(sheetTemplates, "ots") && AnyHolds(sheetTemplates, "xltx") && !AnyHolds(sheetTemplates, "xltm") &&
              !AnyHolds(sheetTemplates, "xlsx"),
          "... a spreadsheet: .ots and .xltx");
    Check(AnyHolds(deckTemplates, "potx") && AnyHolds(deckTemplates, "otp") && !AnyHolds(deckTemplates, "pptx") &&
              !AnyHolds(deckTemplates, "odp"),
          "... a presentation: .potx and .otp");
    FileDialogOptions sheetSave, documentSave, deckSave;
    UltraCanvasFileLoader::AddSpreadsheetSaveTypes(sheetSave);
    UltraCanvasFileLoader::AddTextDocumentSaveTypes(documentSave);
    UltraCanvasFileLoader::AddPresentationSaveTypes(deckSave);
    Check(AnyHolds(sheetSave, "ods") && AnyHolds(sheetSave, "xlsx") && AnyHolds(sheetSave, "csv") &&
              !AnyHolds(sheetSave, "ots") && !AnyHolds(sheetSave, "xltx") && !AnyHolds(sheetSave, "xls"),
          "Save as, a spreadsheet: .ods, .xlsx, .csv - no template, no .xls");
    Check(!AnyHolds(documentSave, "ott") && !AnyHolds(documentSave, "dotx") && !AnyHolds(deckSave, "potx") &&
              !AnyHolds(deckSave, "otp"),
          "... a document or a deck: no template");
    Check(AnyHolds(open, "ott") && AnyHolds(open, "dotx") && AnyHolds(open, "ots") && AnyHolds(open, "xltx") &&
              AnyHolds(open, "otp") && AnyHolds(open, "potx"),
          "the Open dialog offers the templates");

    std::filesystem::remove_all(dir, ec);
    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
