// Tests/FileLoaderOfficeKindTest.cpp
// What an office suite opens a file as (UltraCanvasFileLoader::
// DetectOfficeDocumentKind): by its extension, and by what its package holds
// when the name says nothing (an OpenDocument mimetype, an Office Open XML
// main part). And the Open dialog's types for the three kinds
// (AddOfficeDocumentLoadTypes), where a deck is a presentation, not a
// document as AddTextDocumentLoadTypes offers it.
//
// Usage: FileLoaderOfficeKindTest
// Exit code is the number of failed checks.

#include "UltraCanvasFileLoader.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSpreadsheet.h"

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

    std::filesystem::remove_all(dir, ec);
    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
