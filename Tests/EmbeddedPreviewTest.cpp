// Tests/EmbeddedPreviewTest.cpp
// The preview bitmap vector documents carry inside themselves - what the
// filer thumbnails a Xara or CorelDRAW file from, since neither has a
// renderer that works without a window.
//
// Checks the format gate, the two extractors against the repository's own
// sample documents, and that what comes out really is a decodable image.
// Version: 1.0.0
// Last Modified: 2026-09-02
// Author: UltraCanvas Framework

#include "UltraCanvasEmbeddedPreview.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasSupportedFormats.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

// The magic numbers of the three formats a preview record may hold.
bool LooksLikeImage(const std::vector<uint8_t>& b) {
    if (b.size() < 8) return false;
    const bool png = b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G';
    const bool jpeg = b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF;
    const bool gif = b[0] == 'G' && b[1] == 'I' && b[2] == 'F' && b[3] == '8';
    return png || jpeg || gif;
}

// Every sample of one directory must yield a preview that decodes.
void CheckSamples(const fs::path& dir, const std::string& extension) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        std::cout << "  [SKIP] " << dir.string() << " not present\n";
        return;
    }
    int seen = 0;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
        if (ext != extension) continue;
        ++seen;
        const std::string path = entry.path().string();
        std::vector<uint8_t> bytes = ExtractEmbeddedPreviewBytes(path);
        Check(!bytes.empty(),
              entry.path().filename().string() + ": carries a preview");
        if (bytes.empty()) continue;
        Check(LooksLikeImage(bytes),
              entry.path().filename().string() + ": preview is a PNG/JPEG/GIF");
        auto img = UCImage::LoadFromMemory(bytes);
        Check(img && img->GetWidth() > 0 && img->GetHeight() > 0,
              entry.path().filename().string() + ": preview decodes");
    }
    Check(seen > 0, dir.string() + ": samples found");
}

} // namespace

int main(int argc, char** argv) {
    UCImage::InitializeImageSubsysterm(argv[0]);

    // The repository root: passed in, or the compiled-in path of this source.
    fs::path root = (argc > 1) ? fs::path(argv[1]) : fs::path(UC_MEDIA_DIR);

    std::cout << "\n=== Embedded preview: the format gate ===\n";
    Check(FormatCarriesEmbeddedPreview("drawing.xar"), "xar carries one");
    Check(FormatCarriesEmbeddedPreview("SITE.WEB"), "web carries one (any case)");
    Check(FormatCarriesEmbeddedPreview("wix"), "a bare extension is accepted");
    Check(FormatCarriesEmbeddedPreview("/tmp/a.b/logo.cdr"), "cdr carries one");
    Check(FormatCarriesEmbeddedPreview("template.cdt"), "cdt carries one");
    Check(!FormatCarriesEmbeddedPreview("photo.png"), "png does not");
    Check(!FormatCarriesEmbeddedPreview("drawing.svg"), "svg does not");
    Check(!FormatCarriesEmbeddedPreview(""), "the empty path does not");

    std::cout << "\n=== A file that is not of the format ===\n";
    Check(ExtractEmbeddedPreviewBytes("does-not-exist.xar").empty(),
          "a missing file yields no preview");
    Check(ExtractEmbeddedPreviewBytes("").empty(),
          "an empty path yields no preview");

    std::cout << "\n=== Xara documents ===\n";
    CheckSamples(root / "xar", "xar");

    std::cout << "\n=== CorelDRAW documents ===\n";
    CheckSamples(root / "cdr", "cdr");

    // The filer decides whether to try at all from the format inventory; a
    // regression there is what silently strips every thumbnail.
    std::cout << "\n=== The image pipeline's own answer ===\n";
    for (const char* ext : {"png", "jpg", "jpeg", "gif", "bmp", "tiff", "tif",
                            "webp", "qoi", "ico", "svg", "svgz"}) {
        Check(UltraCanvasSupportedFormats::CanImagePipelineLoad(ext),
              std::string(ext) + " loads through the image pipeline");
    }
    Check(!UltraCanvasSupportedFormats::CanImagePipelineLoad("xar"),
          "xar does not (it previews from its embedded bitmap)");
    Check(!UltraCanvasSupportedFormats::CanImagePipelineLoad("txt"),
          "txt does not");

    std::cout << "\n"
              << (g_failures == 0 ? "All checks passed\n"
                                  : std::to_string(g_failures) + " check(s) FAILED\n");
    return g_failures == 0 ? 0 : 1;
}
