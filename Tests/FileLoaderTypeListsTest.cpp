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
//     and refuses a type it does not write, with a reason;
//   - PixelFX's GetSupportedLoadFormats / GetSupportedSaveFormats are these
//     lists, not a fixed one of its own;
//   - HEIC and AVIF are offered where they decode: libvips' HEIF loader takes
//     both wherever libheif is, but libheif decodes them only with its HEVC /
//     AV1 plug-ins (media/images/test_small.heic and .avif decide: the
//     size of test_small.png, and not black).
//
// Headless.
// Version: 1.2.0 - PixelFX's format lists are FileLoader's
// Version: 1.1.0 - HEIC and AVIF offered where they decode
// Version: 1.0.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework

#include "UltraCanvasFileLoader.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasMediaCodecRegistry.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasRichDocument.h"
#ifdef HAS_LIBVIPS
#include "PixelFX/PixelFX.h"
#endif

#include <algorithm>
#include <cstdint>
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

// A picture's mean level (red, green and blue as stored, premultiplied,
// 0..255) and size, or -1 when it does not load. A load alone proves
// nothing: libvips hands back a HEIC whose codec libheif lacks as a black
// picture of the right size.
double MeanLevel(const std::string& path, int& width, int& height) {
    auto image = UCImageRaster::Load(path, true);
    auto pixmap = image && image->IsValid() ? image->CreatePixmap(image->GetWidth(), image->GetHeight()) : nullptr;
    if (!pixmap || !pixmap->IsValid() || !pixmap->GetPixelData()) return -1.0;
    width = pixmap->GetWidth();
    height = pixmap->GetHeight();
    const uint32_t* argb = pixmap->GetPixelData();
    double sum = 0.0;
    for (size_t i = 0, n = static_cast<size_t>(width) * height; i < n; ++i)
        sum += ((argb[i] >> 16) & 0xFF) + ((argb[i] >> 8) & 0xFF) + (argb[i] & 0xFF);
    return width > 0 && height > 0 ? sum / (3.0 * width * height) : -1.0;
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

    // ----- pictures: offered where they decode -----
    // A HEIC this build cannot decode, offered in an Open dialog, is a file
    // that opens black. The libvips loader's file endings say yes wherever
    // libheif is; the decode says whether its codec is. test_small.png is
    // the same picture, the reference for its size. Levels are not compared:
    // the PNG and the AVIF carry transparency (premultiplied, so darker),
    // the HEIC none; a picture that did not decode is black.
    const std::vector<std::string> pictures = UltraCanvasFileLoader::GetSupportedLoadExtensions(MediaFormatCategory::Bitmap);
    int refWidth = 0, refHeight = 0;
    const double reference = MeanLevel(std::string(UC_MEDIA_DIR) + "/images/test_small.png", refWidth, refHeight);
    Check(reference > 8.0, "the reference picture (test_small.png) loads, and is not black");
    for (const std::string type : {"heic", "avif"}) {
        int width = 0, height = 0;
        const double level = MeanLevel(std::string(UC_MEDIA_DIR) + "/images/test_small." + type, width, height);
        const bool decodes = level > 8.0 && width == refWidth && height == refHeight;
        const bool offered = std::find(pictures.begin(), pictures.end(), type) != pictures.end();
        std::cout << "  " << type << ": " << width << " x " << height << ", level " << level << " (the PNG: " << refWidth
                  << " x " << refHeight << ", level " << reference << ")\n";
#ifdef UC_TEST_HAS_LIBHEIF
        Check(offered == decodes, type + (decodes ? ": decodes here and is offered" : ": does not decode here and is not offered"));
#else
        // Without libheif's headers the inventory goes by the loader's file
        // endings: it cannot leave out what does not decode
        Check(offered || !decodes, type + (decodes ? ": decodes here and is offered" : ": does not decode here"));
#endif
    }

#ifdef HAS_LIBVIPS
    // ----- PixelFX offers FileLoader's lists -----
    const std::vector<std::string> pfxLoad = PixelFX::FileIO::GetSupportedLoadFormats();
    const std::vector<std::string> pfxSave = PixelFX::FileIO::GetSupportedSaveFormats();
    auto inList = [](const std::vector<std::string>& list, const std::string& ext) {
        return std::find(list.begin(), list.end(), ext) != list.end();
    };
    bool everyPicture = !pictures.empty();
    for (const std::string& ext : pictures) everyPicture = everyPicture && inList(pfxLoad, ext);
    Check(everyPicture, "PixelFX reads every picture type FileLoader reads (" + std::to_string(pictures.size()) + ")");
    Check(inList(pfxLoad, "heic") == inList(pictures, "heic") && inList(pfxLoad, "ico") && inList(pfxLoad, "qoi"),
          "... HEIC only where FileLoader has it, ICO and QOI through the framework's loaders");
    Check(!inList(pfxLoad, "docx") && !inList(pfxLoad, "odt") && !inList(pfxLoad, "raw"),
          "... and no document libvips does not open, nor the old list's \"raw\"");
    Check(inList(pfxLoad, "svg") == UltraCanvas::VipsCanLoad(".svg"), "... SVG where libvips opens it");
    Check(pfxSave == UltraCanvasFileLoader::GetSupportedSaveExtensions(MediaFormatCategory::Bitmap),
          "PixelFX writes the picture types FileLoader writes");
    Check(PixelFX::FileIO::IsFormatSupported(".PNG") && PixelFX::FileIO::IsFormatSupported("jpeg") &&
          !PixelFX::FileIO::IsFormatSupported("docx"),
          "IsFormatSupported: a type it reads, with or without its dot");
#endif

    std::cout << (g_failures == 0 ? "All type list checks passed.\n" : "Type list checks FAILED.\n");
    return g_failures == 0 ? 0 : 1;
}
