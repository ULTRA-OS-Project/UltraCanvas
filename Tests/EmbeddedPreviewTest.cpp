// Tests/EmbeddedPreviewTest.cpp
// The preview bitmap vector documents carry inside themselves - what the
// filer thumbnails a Xara, CorelDRAW or PostScript file from, since none of
// them has a renderer that works without a window.
//
// Checks the format gate, the extractors against the repository's own sample
// documents and against EPS files written here, and that what comes out
// really is a decodable image.
// Version: 1.1.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework

#include "UltraCanvasEmbeddedPreview.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasSupportedFormats.h"

#include <cstdint>
#include <algorithm>
#include <filesystem>
#include <fstream>
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

// ===== EPS SAMPLES WRITTEN FOR THE TEST =====
// The repository carries no EPS files, and the two preview mechanisms are
// exactly specified, so the samples are written here: what goes in is known
// byte for byte, which is what makes the polarity check below meaningful.

void WriteFile(const fs::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
}

void Append(std::vector<uint8_t>& out, const std::string& text) {
    out.insert(out.end(), text.begin(), text.end());
}

// An ASCII EPS with an EPSI preview: 4x2 samples, 8 bits each, ink coverage
// (0 = white). The two rows are mirror images of each other.
std::vector<uint8_t> MakeEpsiSample() {
    std::vector<uint8_t> out;
    Append(out, "%!PS-Adobe-3.0 EPSF-3.0\n");
    Append(out, "%%BoundingBox: 0 0 4 2\n");
    Append(out, "%%BeginPreview: 4 2 8 2\n");
    Append(out, "% 004080FF\n");
    Append(out, "% FF804000\n");
    Append(out, "%%EndPreview\n");
    Append(out, "%%EndComments\n");
    Append(out, "0 0 moveto 4 2 lineto stroke\n");
    Append(out, "%%EOF\n");
    return out;
}

// A DOS EPS binary: the 30-byte header, then the PostScript, then a TIFF
// section. The TIFF is a stand-in - the extractor hands the bytes to the
// image pipeline untouched, so what this checks is that the right slice
// comes back.
std::vector<uint8_t> MakeDosEpsSample(const std::vector<uint8_t>& tiff) {
    const std::string ps = "%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 4 2\n";
    const uint32_t psOffset = 30;
    const uint32_t psLength = static_cast<uint32_t>(ps.size());
    const uint32_t tiffOffset = psOffset + psLength;
    const uint32_t tiffLength = static_cast<uint32_t>(tiff.size());
    std::vector<uint8_t> out(30, 0);
    out[0] = 0xC5; out[1] = 0xD0; out[2] = 0xD3; out[3] = 0xC6;
    auto put32 = [&out](int at, uint32_t v) {
        out[at]     = static_cast<uint8_t>(v & 0xFF);
        out[at + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
        out[at + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
        out[at + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
    };
    put32(4, psOffset);
    put32(8, psLength);
    put32(12, 0);            // no Windows Metafile section
    put32(16, 0);
    put32(20, tiffOffset);
    put32(24, tiffLength);
    Append(out, ps);
    out.insert(out.end(), tiff.begin(), tiff.end());
    return out;
}

void CheckPostScriptPreviews(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        std::cout << "  [SKIP] cannot write to " << dir.string() << "\n";
        return;
    }

    // ----- EPSI (the hex preview in the comment block) -----
    const fs::path epsi = dir / "epsi-sample.eps";
    WriteFile(epsi, MakeEpsiSample());
    std::vector<uint8_t> preview = ExtractEmbeddedPreviewBytes(epsi.string());
    Check(!preview.empty(), "eps: the EPSI preview is found");
    const std::string header = "P5\n4 2\n255\n";
    const bool headerOk = preview.size() == header.size() + 8 &&
            std::equal(header.begin(), header.end(), preview.begin());
    Check(headerOk, "eps: it comes out as a 4x2 greyscale PGM");
    if (headerOk) {
        const uint8_t* px = preview.data() + header.size();
        // Ink coverage inverted into grey: 0x00 is white, 0xFF is black.
        Check(px[0] == 255 && px[1] == 191 && px[2] == 127 && px[3] == 0,
              "eps: the first row runs white to black");
        Check(px[4] == 0 && px[5] == 127 && px[6] == 191 && px[7] == 255,
              "eps: the second row runs black to white");
    }
    auto img = UCImage::LoadFromMemory(preview);
    Check(img && img->GetWidth() == 4 && img->GetHeight() == 2,
          "eps: the PGM decodes through the image pipeline");

    // ----- DOS EPS binary header (the TIFF section) -----
    const std::vector<uint8_t> tiff = {'I', 'I', 0x2A, 0x00, 0x08, 0x00,
                                       0x00, 0x00, 0x01, 0x02, 0x03, 0x04};
    const fs::path dos = dir / "dos-sample.eps";
    WriteFile(dos, MakeDosEpsSample(tiff));
    preview = ExtractEmbeddedPreviewBytes(dos.string());
    Check(preview == tiff, "eps: the TIFF section comes back byte for byte");

    // ----- A plain EPS carries nothing -----
    const fs::path plain = dir / "plain-sample.eps";
    WriteFile(plain, std::vector<uint8_t>{});
    {
        std::ofstream f(plain);
        f << "%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 4 2\n"
             "0 0 moveto 4 2 lineto stroke\n%%EOF\n";
    }
    Check(ExtractEmbeddedPreviewBytes(plain.string()).empty(),
          "eps: a file without a preview yields none");

    fs::remove_all(dir, ec);
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
    Check(FormatCarriesEmbeddedPreview("figure.eps"), "eps carries one");
    Check(FormatCarriesEmbeddedPreview("figure.EPSF"), "epsf carries one");
    Check(FormatCarriesEmbeddedPreview("page.ps"), "ps carries one");
    Check(FormatCarriesEmbeddedPreview("artwork.ai"), "ai carries one");
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

    std::cout << "\n=== PostScript documents (EPSI and DOS EPS) ===\n";
    CheckPostScriptPreviews(fs::temp_directory_path() / "uc-eps-preview-test");

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
