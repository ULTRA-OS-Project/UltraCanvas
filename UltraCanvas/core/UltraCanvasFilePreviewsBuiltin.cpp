// core/UltraCanvasFilePreviewsBuiltin.cpp
// The preview providers of the formats compiled into the framework
// (UltraCanvasFilePreviews.h): a PDF's first page, an e-book's cover, a
// video's poster frame, a word-processing document's first page and its text,
// a PowerPoint or OpenDocument deck's first slide and its text. Each
// was a function inside the Filer until the registry existed; it is the same
// code, registered under the format's extensions instead of called by name.
//
// Kept in a file of their own, so the registry itself depends on no format,
// and a format that moves into a runtime module takes its provider with it
// and registers it from the module's init.
//
// Version: 1.4.1 - an .odp deck's first slide is drawn with its pictures
// Version: 1.4.0 - a deck's thumbnail is its first slide as drawn (RenderPptxSlide), not a page of
//                  its outline
// Version: 1.3.0 - an OpenDocument presentation (.odp, .otp, .fodp) previews its slides' text,
//                  like a PowerPoint deck
// Version: 1.2.0 - a word-processing document (.odt, .docx, .doc) draws its first page as the
//                  document lays it out - its fonts, pictures, colours, header and footer - not
//                  a page of its text lines: the "word-processing" provider renders thumbnails
// Version: 1.1.0 - a PDF's first page on white paper: the page the engine left transparent showed
//                  the tile under it (a dark page with white patches on a dark tile)
// Version: 1.0.0
// Last Modified: 2026-11-22
// Author: UltraCanvas Framework

#include "UltraCanvasFilePreviews.h"
#include "UltraCanvasRichDocument.h"
#include "UltraCanvasRichTextEdit.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasVideoThumbnail.h"
#include "UltraCanvasMediaCodecRegistry.h"
#include "UltraCanvasPresentationPptx.h"
#include "UltraCanvasPptxSlideRender.h"
#include "UltraCanvasPresentationOdp.h"
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

// The page on paper: the engine leaves what a PDF does not paint transparent
// - the paper of nearly every page - so a thumbnail drawn on a dark tile
// showed a dark page with the page's own white boxes as light patches. A
// page is paper: what it leaves transparent is white, what it paints keeps
// its colour, and the picture is opaque.
void LayOnWhitePaper(std::vector<uint8_t>& rgba, int w, int h, int stride) {
    for (int y = 0; y < h; ++y) {
        uint8_t* px = rgba.data() + static_cast<size_t>(y) * stride;
        for (int x = 0; x < w; ++x, px += 4) {
            const unsigned a = px[3];
            if (a == 255) continue;
            for (int c = 0; c < 3; ++c) px[c] = static_cast<uint8_t>((px[c] * a + 255u * (255u - a) + 127u) / 255u);
            px[3] = 255;
        }
    }
}

std::shared_ptr<UCPixmap> RenderPdfFirstPage(const std::string& path, const FileThumbnailRequest& r) {
    const int maxDim = std::max(16, DeviceEdge(std::max(r.width, r.height), r.scale));
    std::unique_ptr<IPDFDocument> doc = OpenPDF(path);
    if (!doc || doc->GetPageCount() < 1) return nullptr;
    PDFRenderedPage page = doc->RenderThumbnail(1, maxDim);
    if (!page.IsValid() || page.colorMode != PDFColorMode::RGBA) return nullptr;
    LayOnWhitePaper(page.pixels, page.width, page.height, page.stride);
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

// ===== Word processing: the first page, as a picture =====
// The page itself, laid out by the rich-text element exactly as its PDF
// export and its print pages are (UltraCanvasRichTextEdit::BeginPrintLayout /
// RenderPrintPage): the document's own page size and margins, its fonts and
// sizes, pictures, colours, tables, header and footer. What the Filer drew
// before was a page of the text's lines in one small grey font, which said
// what a document says but not what it looks like.
//
// Serialized: the layout goes through a render context and the process-wide
// font machinery, as the Filer's vector drawings do (RenderVectorDrawingPixmap
// holds a lock for the same reason). The element is made here, never shown,
// and set to a light style of its own, so the page is paper in either theme.
//
// Only the first page is wanted, so a long document is cut to its first
// blocks before it is laid out: page 1 of a 400-page report costs what page 1
// of a letter does. kFirstPageBlockCap is far more paragraphs than one page
// holds.
constexpr size_t kFirstPageBlockCap = 200;
constexpr double kPixelsPerPoint = 96.0 / 72.0;

// A document's pictures are decoded by libvips, which knows its loaders only
// once it has been initialised. An application has done that long before a
// thumbnail is asked for; a headless caller (a test, a command-line tool) may
// not have, and an uninitialised libvips crashes on the first picture. Same
// gate the format inventory and the vector rasterizer use.
bool EnsureImageSubsystem() {
#ifdef HAS_LIBVIPS
    static const bool ok = UCImageRaster::InitializeImageSubsysterm(nullptr);
    return ok;
#else
    return true;
#endif
}

std::shared_ptr<UCPixmap> RenderWordFirstPage(const std::string& path, const FileThumbnailRequest& r) {
    if (r.width <= 0 || r.height <= 0) return nullptr;
    if (!EnsureImageSubsystem()) return nullptr;
    auto document = std::make_shared<UCRichDocument>();
    std::string error;
    if (!UCWordDocumentIO::Load(path, *document, error)) return nullptr;
    if (document->blocks.size() > kFirstPageBlockCap) document->blocks.resize(kFirstPageBlockCap);

    static std::mutex layoutMutex;
    std::lock_guard<std::mutex> lock(layoutMutex);

    auto layout = std::make_shared<UltraCanvasRichTextEdit>("WordPagePreview", 0, 0, 1, 1);
    // The editor's own defaults (its base font included), never the theme's:
    // ink on white paper whatever the desktop looks like.
    RichTextEditStyle style = layout->GetStyle();
    const RichTextEditStyle paper;
    style.textColor = paper.textColor;
    style.backgroundColor = paper.backgroundColor;
    style.pageColor = paper.pageColor;
    style.linkColor = paper.linkColor;
    style.quoteBarColor = paper.quoteBarColor;
    style.quoteTextColor = paper.quoteTextColor;
    style.codeTextColor = paper.codeTextColor;
    style.codeBackgroundColor = paper.codeBackgroundColor;
    style.codeBorderColor = paper.codeBorderColor;
    style.ruleColor = paper.ruleColor;
    style.listMarkerColor = paper.listMarkerColor;
    style.tableBorderColor = paper.tableBorderColor;
    style.imagePlaceholderColor = paper.imagePlaceholderColor;
    layout->SetStyle(style);
    layout->SetShowFontNotice(false);
    layout->SetDocument(document);

    const RichPageSetup page = layout->GetEffectivePageSetup();
    const double pageW = page.widthPt * kPixelsPerPoint;
    const double pageH = page.heightPt * kPixelsPerPoint;
    if (pageW < 1.0 || pageH < 1.0) return nullptr;
    // The page fitted into the box asked for, at the display's scale.
    const double fit = std::min(DeviceEdge(r.width, r.scale) / pageW,
                                DeviceEdge(r.height, r.scale) / pageH);
    const int w = std::max(1, static_cast<int>(std::lround(pageW * fit)));
    const int h = std::max(1, static_cast<int>(std::lround(pageH * fit)));

    auto pixmap = std::make_shared<UCPixmap>();
    if (!pixmap->Init(w, h)) return nullptr;
    std::unique_ptr<IRenderContext> ctx = CreateRenderContext(Size2Di(w, h), nullptr);
    if (!ctx) return nullptr;
    ctx->Clear(Colors::White);
    // Laid out unscaled, so lines break where they do on the full-size page,
    // then drawn scaled down to the thumbnail.
    const int pages = layout->BeginPrintLayout(ctx.get());
    if (pages > 0) {
        ctx->PushState();
        ctx->Scale(fit, fit);
        layout->RenderPrintPage(ctx.get(), 0);
        ctx->PopState();
    }
    layout->EndPrintLayout();
    if (pages <= 0) return nullptr;
    ctx->FlushToSurface(pixmap->GetSurface(), Point2Dd(0, 0));
    pixmap->MarkDirty();
    return pixmap;
}

// ===== PowerPoint and OpenDocument decks: the slide outline =====
// Each slide's number and title, then its bullets, tables and notes, as
// UltraCanvasPresentationPptx outlines a deck. A protected deck answers false.
bool ReadPresentation(const std::string& path, UCRichDocument& document) {
    PptxPresentation deck;
    std::string error;
    const bool read = IsOdpExtension(path) ? ReadOdpPresentation(path, deck, error)
                                           : ReadPptxPresentation(path, deck, error);
    if (!read) return false;
    document = PptxPresentationToRichDocument(deck);
    return true;
}

// ...and its first slide as drawn, for the thumbnail: as wide as fits the
// tile at the slide's shape. A protected deck answers null (the type glyph).
std::shared_ptr<UCPixmap> RenderDeckFirstSlide(const std::string& path, const FileThumbnailRequest& r) {
    if (r.width <= 0 || r.height <= 0) return nullptr;
    PptxPresentation deck;
    std::string error;
    const bool read = IsOdpExtension(path) ? ReadOdpPresentation(path, deck, error, "", true)
                                           : ReadPptxPresentation(path, deck, error, "", true);
    if (!read || deck.slides.empty()) return nullptr;
    const double slideW = deck.slideWidth > 0 ? static_cast<double>(deck.slideWidth) : 12192000.0;
    const double slideH = deck.slideHeight > 0 ? static_cast<double>(deck.slideHeight) : 6858000.0;
    const double fitWidth = std::min(static_cast<double>(r.width), r.height * slideW / slideH);
    return RenderPptxSlide(deck, 0, DeviceEdge(static_cast<int>(std::lround(fitWidth)), r.scale));
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
        // The first page as a picture. Not a .tex: its page is what a TeX
        // engine would set, which this layout is not, so it keeps its outline
        // of title and sections.
        word.thumbnailExtensions = {"odt", "docx", "doc"};
        word.RenderThumbnail = RenderWordFirstPage;
        RegisterFilePreviewProvider(std::move(word));

        FilePreviewProvider deck;
        deck.name = "powerpoint";
        deck.documentExtensions = {"pptx", "pptm", "ppsx", "ppsm", "potx", "potm", "odp", "otp", "fodp"};
        deck.ReadDocument = ReadPresentation;
        deck.thumbnailExtensions = deck.documentExtensions;
        deck.RenderThumbnail = RenderDeckFirstSlide;
        RegisterFilePreviewProvider(std::move(deck));
    });
}

} // namespace UltraCanvas
