// core/UltraCanvasFilePreviewsBuiltin.cpp
// The preview providers of the formats compiled into the framework
// (UltraCanvasFilePreviews.h): a PDF's first page, an e-book's cover, a
// video's poster frame, a Word document's and a PowerPoint deck's text. Each
// was a function inside the Filer until the registry existed; it is the same
// code, registered under the format's extensions instead of called by name.
//
// Kept in a file of their own, so the registry itself depends on no format,
// and a format that moves into a runtime module takes its provider with it
// and registers it from the module's init.
//
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "UltraCanvasFilePreviews.h"
#include "UltraCanvasRichDocument.h"
#include "UltraCanvasVideoThumbnail.h"
#include "UltraCanvasMediaCodecRegistry.h"
#include "UltraCanvasPresentationPptx.h"
#include "Plugins/Documents/Word/UltraCanvasWordDocumentIO.h"
#include "Documents/eBook/EPUBEngine.h"
#include "Documents/eBook/MOBIEngine.h"
#ifdef ULTRACANVAS_PLUGIN_PDF
#include "Plugins/Documents/UltraCanvasPDF.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>

namespace UltraCanvas {

namespace {

std::string LowerExtensionOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    const size_t dot = path.rfind('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

int DeviceEdge(int logical, float scale) {
    return std::max(1, static_cast<int>(std::lround(logical * std::max(1.0f, scale))));
}

// ===== PDF: the first page =====
// Each call opens its own document, and with it its own engine context, which
// is what makes it safe to run on several thumbnail workers at once.
#ifdef ULTRACANVAS_PLUGIN_PDF
// Straight (non-premultiplied) RGBA rows into a fresh pixmap, which holds
// premultiplied ARGB32 as every Cairo-backed surface here expects.
std::shared_ptr<UCPixmap> PixmapFromRGBA(const uint8_t* rgba, int w, int h, int srcStride) {
    if (!rgba || w <= 0 || h <= 0) return nullptr;
    auto pm = std::make_shared<UCPixmap>();
    if (!pm->Init(w, h)) return nullptr;
    uint32_t* dst = pm->GetPixelData();
    if (!dst) return nullptr;
    for (int y = 0; y < h; ++y) {
        const uint8_t* src = rgba + static_cast<size_t>(y) * srcStride;
        uint32_t* row = dst + static_cast<size_t>(y) * w;
        for (int x = 0; x < w; ++x, src += 4) {
            const uint8_t r = src[0], g = src[1], b = src[2], a = src[3];
            row[x] = (uint32_t(a) << 24)
                   | (uint32_t((uint16_t(r) * a + 127) / 255) << 16)
                   | (uint32_t((uint16_t(g) * a + 127) / 255) << 8)
                   |  uint32_t((uint16_t(b) * a + 127) / 255);
        }
    }
    pm->MarkDirty();
    return pm;
}

std::shared_ptr<UCPixmap> RenderPdfFirstPage(const std::string& path, const FileThumbnailRequest& r) {
    const int maxDim = std::max(16, DeviceEdge(std::max(r.width, r.height), r.scale));
    std::unique_ptr<IPDFDocument> doc = OpenPDF(path);
    if (!doc || doc->GetPageCount() < 1) return nullptr;
    PDFRenderedPage page = doc->RenderThumbnail(1, maxDim);
    if (!page.IsValid() || page.colorMode != PDFColorMode::RGBA) return nullptr;
    return PixmapFromRGBA(page.pixels.data(), page.width, page.height, page.stride);
}
#endif

// ===== E-books: the cover =====
// The picture the book declares, read without unpacking a chapter: three
// archive entries of an EPUB, the record list, record 0 and the cover record
// of a Mobipocket / Kindle book. Each read owns its file and parser.
bool IsKindleExtension(const std::string& ext) {
    return ext == "mobi" || ext == "prc" || ext == "azw" || ext == "azw3";
}

std::shared_ptr<UCPixmap> RenderEBookCover(const std::string& path, const FileThumbnailRequest& r) {
    const std::string ext = LowerExtensionOf(path);
    std::vector<uint8_t> bytes = IsKindleExtension(ext) ? MOBIEngine::ReadCoverImageFromFile(path)
                                                        : EPUBEngine::ReadCoverImageFromFile(path);
    if (bytes.empty()) return nullptr;
    auto img = UCImage::LoadFromMemory(bytes);
    if (!img || img->GetWidth() <= 0 || img->GetHeight() <= 0) return nullptr;
    // The pixmap is this image's alone (an image loaded from memory gets a
    // cache key no other caller can ask for), so a caller may shade it.
    return img->GetPixmap(r.width, r.height, r.fit, r.scale);
}

// ===== Video: the poster frame =====
// A short way into the clip (past a black intro). May block for a few seconds
// on a cold file, which is what the thumbnail workers are for.
std::shared_ptr<UCPixmap> RenderVideoPoster(const std::string& path, const FileThumbnailRequest& r) {
    VideoThumbnailRequest request;
    request.maxWidth = DeviceEdge(r.width, r.scale);
    request.maxHeight = DeviceEdge(r.height, r.scale);
    return CaptureVideoThumbnailPixmap(path, request);
}

// ===== Word processing: the document =====
// ODT, DOCX, legacy DOC and LaTeX through the one rich-document reader (a .tex
// shows its title and sections, not its markup). A password-protected .docx
// cannot be read and answers false.
bool ReadWordDocument(const std::string& path, UCRichDocument& document) {
    UCRichDocument doc;
    std::string error;
    if (!UCWordDocumentIO::Load(path, doc, error)) return false;
    // The first page's header and footer belong to what the page shows.
    if (!doc.FurnitureForPage(0).IsEmpty()) doc = doc.WithFirstPageFurnitureInline();
    document = std::move(doc);
    return true;
}

// ===== PowerPoint: the slide outline =====
// Each slide's number and title, then its bullets, tables and notes, as
// UltraCanvasPresentationPptx outlines a deck. A protected deck answers false.
bool ReadPresentation(const std::string& path, UCRichDocument& document) {
    PptxPresentation deck;
    std::string error;
    if (!ReadPptxPresentation(path, deck, error)) return false;
    document = PptxPresentationToRichDocument(deck);
    return true;
}

} // namespace

void RegisterBuiltinFilePreviewProviders() {
    static std::once_flag once;
    std::call_once(once, [] {
#ifdef ULTRACANVAS_PLUGIN_PDF
        FilePreviewProvider pdf;
        pdf.name = "pdf";
        pdf.thumbnailExtensions = {"pdf"};
        pdf.Available = [] { return !PDFEngineFactory::Available().empty(); };
        pdf.RenderThumbnail = RenderPdfFirstPage;
        RegisterFilePreviewProvider(std::move(pdf));
#endif

        FilePreviewProvider ebook;
        ebook.name = "ebook-cover";
        ebook.thumbnailExtensions = {"epub", "mobi", "prc", "azw", "azw3"};
        ebook.RenderThumbnail = RenderEBookCover;
        RegisterFilePreviewProvider(std::move(ebook));

        // The containers the video backend demuxes, as the media codec
        // registry lists them: none in a build without video, where a
        // poster frame could not be captured anyway.
        RegisterBuiltinMediaCodecs();
        FilePreviewProvider video;
        video.name = "video-poster";
        for (const MediaCodecRegistration& codec : GetRegisteredMediaCodecs(MediaCodecKind::Video)) {
            if (!codec.canDecode) continue;
            video.thumbnailExtensions.push_back(codec.extension);
            video.thumbnailExtensions.insert(video.thumbnailExtensions.end(),
                                             codec.aliases.begin(), codec.aliases.end());
        }
        video.RenderThumbnail = RenderVideoPoster;
        if (!video.thumbnailExtensions.empty()) RegisterFilePreviewProvider(std::move(video));

        FilePreviewProvider word;
        word.name = "word-processing";
        word.documentExtensions = {"odt", "docx", "doc", "tex"};
        word.ReadDocument = ReadWordDocument;
        RegisterFilePreviewProvider(std::move(word));

        FilePreviewProvider deck;
        deck.name = "powerpoint";
        deck.documentExtensions = {"pptx", "pptm", "ppsx", "ppsm", "potx", "potm"};
        deck.ReadDocument = ReadPresentation;
        RegisterFilePreviewProvider(std::move(deck));
    });
}

} // namespace UltraCanvas
