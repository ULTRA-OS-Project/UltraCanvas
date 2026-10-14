// Tests/FileLoaderTypeListsTest.cpp
// UltraCanvasFileLoader's format inventory as an Open or Save dialog's file
// types - what an application's dialogs offer instead of lists of their own:
//
//   - AddLoadTypes: one filter with every type the build reads in the
//     categories asked for, none when it reads none;
//   - AddSaveTypes: one filter per type it writes, under its name, the one
//     the proposed file name ends in first;
//   - a codec an application registers (RegisterMediaCodec) joins both;
//   - AddTextDocumentLoadTypes / AddTextDocumentSaveTypes: what
//     LoadTextDocument reads and SaveTextDocument writes - no PDF or e-book
//     the text reader cannot open;
//   - SaveTextDocument writes ODT and DOCX that LoadTextDocument reads back,
//     and refuses a type it does not write, with a reason.
//
// Headless.
// Version: 1.0.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework

#include "UltraCanvasFileLoader.h"
#include "UltraCanvasMediaCodecRegistry.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasRichDocument.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

bool Holds(const FileFilter& filter, const std::string& ext) {
    return std::find(filter.extensions.begin(), filter.extensions.end(), ext) != filter.extensions.end();
}

bool AnyHolds(const FileDialogOptions& opts, const std::string& ext) {
    for (const FileFilter& filter : opts.filters)
        if (Holds(filter, ext)) return true;
    return false;
}

const FileFilter* FilterFor(const FileDialogOptions& opts, const std::string& ext) {
    for (const FileFilter& filter : opts.filters)
        if (Holds(filter, ext)) return &filter;
    return nullptr;
}

} // namespace

int main() {
    std::cout << "FileLoaderTypeListsTest" << std::endl;

    // ----- AddLoadTypes -----
    FileDialogOptions sheets;
    UltraCanvasFileLoader::AddLoadTypes(sheets, "Spreadsheets", {MediaFormatCategory::Spreadsheet});
    Check(sheets.filters.size() == 1 && sheets.filters.front().description == "Spreadsheets",
          "AddLoadTypes: one filter, under the label given");
    Check(!sheets.filters.empty() && Holds(sheets.filters.front(), "ods") && Holds(sheets.filters.front(), "xlsx") &&
          Holds(sheets.filters.front(), "xls") && Holds(sheets.filters.front(), "csv"),
          "... holding every spreadsheet type the build reads, XLS included");

    FileDialogOptions mixed;
    UltraCanvasFileLoader::AddLoadTypes(mixed, "Sheets and documents",
                                        {MediaFormatCategory::Spreadsheet, MediaFormatCategory::Document});
    Check(mixed.filters.size() == 1 && Holds(mixed.filters.front(), "ods") && Holds(mixed.filters.front(), "odt") &&
          Holds(mixed.filters.front(), "markdown"),
          "... several categories in one filter, aliases too");
    const std::vector<std::string>& all = mixed.filters.front().extensions;
    Check(std::count(all.begin(), all.end(), "txt") == 1, "... each extension once");

    FileDialogOptions none;
    UltraCanvasFileLoader::AddLoadTypes(none, "Models", {MediaFormatCategory::Model3D});
    Check(none.filters.empty(), "... and no filter at all when the build reads none of them");

    // ----- AddSaveTypes -----
    FileDialogOptions sheetSave;
    UltraCanvasFileLoader::AddSaveTypes(sheetSave, MediaFormatCategory::Spreadsheet);
    const FileFilter* ods = FilterFor(sheetSave, "ods");
    Check(ods && ods->description == "OpenDocument Spreadsheet" && ods->extensions.size() == 1,
          "AddSaveTypes: one filter per type it writes, under its name");
    Check(AnyHolds(sheetSave, "xlsx") && AnyHolds(sheetSave, "csv") && !AnyHolds(sheetSave, "xls"),
          "... and none for a type it only reads (XLS)");
    FileDialogOptions named;
    named.SetDefaultFileName("table.CSV");
    UltraCanvasFileLoader::AddSaveTypes(named, MediaFormatCategory::Spreadsheet);
    Check(!named.filters.empty() && Holds(named.filters.front(), "csv") && named.filters.size() == sheetSave.filters.size(),
          "... the type the proposed name ends in first (the dialog starts on it), none lost");
    FileDialogOptions docx;
    docx.SetDefaultFileName("letter.docx");
    UltraCanvasFileLoader::AddTextDocumentSaveTypes(docx);
    Check(docx.filters.size() == 2 && Holds(docx.filters[0], "docx") && Holds(docx.filters[1], "odt"),
          "... the text document types too");

    // ----- a codec the application brings joins both -----
    MediaCodecRegistration codec;
    codec.extension = "uvtest";
    codec.aliases = {"uvt"};
    codec.description = "Test container";
    codec.kind = MediaCodecKind::Video;
    codec.canDecode = true;
    codec.canEncode = true;
    codec.provider = "FileLoaderTypeListsTest";
    RegisterMediaCodec(codec);
    FileDialogOptions videoOpen, videoSave;
    UltraCanvasFileLoader::AddLoadTypes(videoOpen, "Video", {MediaFormatCategory::Video});
    UltraCanvasFileLoader::AddSaveTypes(videoSave, MediaFormatCategory::Video);
    const FileFilter* written = FilterFor(videoSave, "uvtest");
    Check(AnyHolds(videoOpen, "uvtest") && AnyHolds(videoOpen, "uvt"),
          "a registered codec joins the Open filter, alias and all");
    Check(written && written->description == "Test container" && Holds(*written, "uvt"),
          "... and the Save types, under its name");
    UnregisterMediaCodec(MediaCodecKind::Video, "uvtest");

    // ----- text documents -----
    FileDialogOptions docOpen;
    UltraCanvasFileLoader::AddTextDocumentLoadTypes(docOpen);
    Check(!docOpen.filters.empty() && docOpen.filters.front().description == "Documents",
          "AddTextDocumentLoadTypes: a \"Documents\" filter first");
    const FileFilter* docs = docOpen.filters.empty() ? nullptr : &docOpen.filters.front();
    Check(docs && Holds(*docs, "odt") && Holds(*docs, "docx") && Holds(*docs, "doc") && Holds(*docs, "tex") &&
          Holds(*docs, "md") && Holds(*docs, "txt"),
          "... with every type LoadTextDocument reads");
    Check(docs && !Holds(*docs, "pdf") && !Holds(*docs, "epub") && !Holds(*docs, "html"),
          "... and nothing it cannot (PDF, e-books, HTML)");
    bool eachNamed = docOpen.filters.size() > 2;
    for (size_t i = 1; i < docOpen.filters.size(); ++i)
        eachNamed = eachNamed && docOpen.filters[i].description != "Documents" &&
                    docOpen.filters[i].extensions.size() < docs->extensions.size();
    Check(eachNamed, "... then each type under its name");

    FileDialogOptions docSave;
    UltraCanvasFileLoader::AddTextDocumentSaveTypes(docSave);
    Check(docSave.filters.size() == 2 && Holds(docSave.filters[0], "odt") && Holds(docSave.filters[1], "docx"),
          "AddTextDocumentSaveTypes: ODT, then DOCX - what SaveTextDocument writes");

    const fs::path dir = fs::temp_directory_path() / "uc-fileloader-type-lists";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const UCRichDocument document = UCRichDocument::FromMarkdown("# A heading\n\nA paragraph of text.\n");
    for (const char* name : {"saved.odt", "saved.docx"}) {
        const std::string path = PathToUtf8(dir / name);
        std::string error;
        Check(UltraCanvasFileLoader::SaveTextDocument(document, path, error), std::string("SaveTextDocument writes ") +
              name + (error.empty() ? "" : ": " + error));
        auto back = UltraCanvasFileLoader::LoadTextDocument(path, error);
        Check(back && back->blocks.size() >= 2, std::string("... which LoadTextDocument reads back"));
    }
    std::string error;
    Check(!UltraCanvasFileLoader::SaveTextDocument(document, PathToUtf8(dir / "saved.tex"), error) && !error.empty(),
          "... and refuses a type it does not write, with a reason");
    fs::remove_all(dir, ec);

    std::cout << (g_failures == 0 ? "All type list checks passed.\n" : "Type list checks FAILED.\n");
    return g_failures == 0 ? 0 : 1;
}
