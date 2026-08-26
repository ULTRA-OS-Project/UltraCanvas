// core/UltraCanvasMediaViewer.cpp
// Implementation of the comprehensive media / photo / document viewer widget.
// See UltraCanvasMediaViewer.h for the feature overview.
// Version: 1.5.0
// Last Modified: 2026-08-23
// V1.5.0: CloseFile() shows nothing and lets go of the file - playback stops and
//   every display backend releases its document, so a host preview pane can move,
//   rename or delete the file it was just showing. StopPlayback() alone never
//   released it, which made a move of the previewed file fail on Windows.
// V1.4.1: The PreviewClip ("5 s clip") video preview is silent for real. The
//   mute is applied before the source is opened (so the engine builds a muted
//   session instead of muting one that may already be wired for sound), and the
//   clip now stays muted when it pauses — the sound returns on the user's own
//   resume, not at a pause the backend may still be deferring.
// Author: UltraCanvas Framework

#include "UltraCanvasMediaViewer.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasBreadcrumb.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasColorSwatchBar.h"  // backdrop palette under transparent images
#include "UltraCanvasApplication.h"
#include "UltraCanvasFileLoader.h"   // FileDialogOptions, DialogResult, FileFilter
#include "UltraCanvasSpreadsheet.h"  // ODS / CSV / TSV (always built into the core lib)
#include "Models/STL/UltraCanvasSTLElement.h"  // STL 3D viewer (GL or 2D fallback)
#include "UltraCanvasTextArea.h"      // text / source / markdown view
#include "UltraCanvasSyntaxTokenizer.h" // resolve source language from extension
#include "UltraCanvasEBookViewer.h"   // EPUB / FB2 / MOBI e-book view
#include "Documents/eBook/TXTEngine.h" // RegisterBuiltinEBookEngines (idempotent)
#ifdef ULTRACANVAS_PLUGIN_PDF
#include "Plugins/Documents/UltraCanvasPDFView.h"
#endif
#ifdef ULTRACANVAS_ENABLE_VIDEO
#include "UltraCanvasVideoPlayerElement.h"
#endif
#ifdef ULTRACANVAS_ENABLE_AUDIO
#include "UltraCanvasAudioPlayerElement.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace UltraCanvas {

static constexpr double kHalfPi = 1.5707963267948966;

// ===========================================================================
// FILE-LOCAL HELPERS
// ===========================================================================

static std::string LowerExt(const std::string& path) {
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return ext;
}

static std::string BaseName(const std::string& path) {
    std::error_code ec;
    fs::path p(path);
    return p.filename().string();
}

static std::string HumanSize(uintmax_t bytes) {
    const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    char buf[48];
    if (u == 0) snprintf(buf, sizeof(buf), "%llu %s", (unsigned long long)bytes, units[u]);
    else        snprintf(buf, sizeof(buf), "%.1f %s", v, units[u]);
    return std::string(buf);
}

// Read a text file into `out`, capped at maxBytes so a huge/binary file can't
// stall the viewer. Returns false if the file can't be opened.
static bool ReadTextFile(const std::string& path, std::string& out,
                         size_t maxBytes = 16u * 1024u * 1024u) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    if (out.size() > maxBytes) {
        out.resize(maxBytes);
        out += "\n\n[... truncated ...]";
    }
    return true;
}

// ----- UCD v2 CONTAINER HEADER -----
// Fixed 28-byte header of an UltraCanvas Document container, per
// Docs/UltraCanvas/UCD-FileFormat-v2.md. The header (and the raw HEIC/PNG
// preview thumbnail that follows it) is never compressed or encrypted, so a
// viewer can identify the file and show a preview without the UCD v2 engine.
struct UCDHeader {
    bool        valid = false;      // signature matched (0x89 'U' 'C' … \r\n 0x1A)
    std::string descriptor;         // "UCDoc", "UCForm", "UCVector", …
    int         versionMajor = 0;
    int         versionMinor = 0;
    uint8_t     bodyEncoding = 0;   // 0 binary, 1 XML text, 2 JSON text
    uint8_t     compression = 0;    // 0 none, 1 deflate, 2 gzip, 3 LZMA
    uint8_t     encryption = 0;     // 0 none, 1 AES-256-GCM, 2 ChaCha20, 3 SuperVault
    uint8_t     flags = 0;          // bit0 thumbnail, bit1 PNG (else HEIC),
                                    // bit2 encrypted, bit3 private
    uint32_t    thumbnailLength = 0;
    uint32_t    extensionLength = 0;

    bool HasThumbnail() const { return (flags & 0x01) && thumbnailLength > 0; }
    bool ThumbnailIsPNG() const { return (flags & 0x02) != 0; }
    bool IsEncrypted() const { return (flags & 0x04) != 0 || encryption != 0; }
    bool IsPrivate() const { return (flags & 0x08) != 0; }
};

static uint32_t ReadLE32(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool ReadUCDHeader(const std::string& path, UCDHeader& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    unsigned char h[28];
    if (!f.read(reinterpret_cast<char*>(h), sizeof(h))) return false;
    if (h[0] != 0x89 || h[1] != 'U' || h[2] != 'C' ||
        h[9] != 0x0D || h[10] != 0x0A || h[11] != 0x1A) return false;
    out.valid = true;
    out.descriptor.assign(reinterpret_cast<const char*>(h + 1), 8);
    if (auto z = out.descriptor.find('\0'); z != std::string::npos)
        out.descriptor.resize(z);
    out.versionMajor    = h[12];
    out.versionMinor    = h[13];
    out.bodyEncoding    = h[14];
    out.compression     = h[15];
    out.encryption      = h[16];
    out.flags           = h[17];
    out.thumbnailLength = ReadLE32(h + 20);
    out.extensionLength = ReadLE32(h + 24);
    return true;
}

// Extract the raw (HEIC/PNG) preview thumbnail stored directly after the fixed
// header + header extension. Length-capped so a corrupt header can't allocate
// wild amounts of memory.
static bool ReadUCDThumbnail(const std::string& path, const UCDHeader& hdr,
                             std::vector<uint8_t>& out) {
    constexpr uint32_t kMaxThumbBytes = 64u * 1024u * 1024u;
    if (!hdr.valid || !hdr.HasThumbnail() || hdr.IsPrivate() ||
        hdr.thumbnailLength > kMaxThumbBytes) return false;
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(28 + (std::streamoff)hdr.extensionLength);
    out.resize(hdr.thumbnailLength);
    return f.read(reinterpret_cast<char*>(out.data()), out.size()) ? true : false;
}

// Friendly name of the primary content type a UCD descriptor announces.
static std::string UCDDescriptorName(const std::string& descriptor) {
    if (descriptor == "UCDoc")    return "multi-page document";
    if (descriptor == "UCForm")   return "saveable / fillable form";
    if (descriptor == "UCVector") return "vector graphics";
    if (descriptor == "UCWindow") return "window / UI description";
    if (descriptor == "UCBitmap") return "bitmap graphics";
    if (descriptor == "UCVideo")  return "video";
    if (descriptor == "UCAudio")  return "audio";
    if (descriptor == "UC3D")     return "3D scene / model";
    return "unknown content type";
}

// Human-readable summary of a *.ucd file for the details popup (and the text
// view when the container carries no preview thumbnail).
static std::string BuildUCDDetailsText(const std::string& path,
                                       const UCDHeader& hdr, bool thumbShown) {
    std::ostringstream os;
    os << "UltraCanvas Document (UCD)\n\n";
    os << "File: " << BaseName(path) << "\n";
    os << "Path: " << path << "\n";
    std::error_code ec;
    auto sz = fs::file_size(path, ec);
    if (!ec) os << "Size: " << HumanSize(sz) << "\n";
    if (!hdr.valid) {
        os << "\nNot a UCD v2 container (no valid signature).\n"
              "It may be a UCD v1 XML/JSON document or a different file.\n";
        return os.str();
    }
    os << "Container: UCD v" << hdr.versionMajor << "." << hdr.versionMinor
       << " (" << hdr.descriptor << " \xE2\x80\x94 "
       << UCDDescriptorName(hdr.descriptor) << ")\n";
    os << "Body encoding: "
       << (hdr.bodyEncoding == 0 ? "binary" :
           hdr.bodyEncoding == 1 ? "XML text (debug)" :
           hdr.bodyEncoding == 2 ? "JSON text (debug)" : "unknown") << "\n";
    os << "Compression: "
       << (hdr.compression == 0 ? "none" :
           hdr.compression == 1 ? "deflate" :
           hdr.compression == 2 ? "gzip" :
           hdr.compression == 3 ? "LZMA" : "unknown") << "\n";
    os << "Encryption: "
       << (hdr.encryption == 0 ? "none" :
           hdr.encryption == 1 ? "AES-256-GCM" :
           hdr.encryption == 2 ? "ChaCha20-Poly1305" :
           hdr.encryption == 3 ? "SuperVault remote authorization" : "unknown")
       << (hdr.IsPrivate() ? " (private)" : "") << "\n";
    if (hdr.HasThumbnail()) {
        os << "Thumbnail: " << (hdr.ThumbnailIsPNG() ? "PNG" : "HEIC") << ", "
           << HumanSize(hdr.thumbnailLength)
           << (thumbShown ? " (shown)" : " (could not be decoded)") << "\n";
    } else {
        os << "Thumbnail: none\n";
    }
    os << "\nPreview only \xE2\x80\x94 full rendering arrives with the UCD v2 engine.\n";
    return os.str();
}

#ifdef HAS_LIBVIPS
// Apply the tone/colour adjustments to a PixelFX image. Pure colour work — no
// geometry. Wrapped per-op in try/catch so an unsupported colourspace for one
// operation (e.g. sharpen on an exotic format) doesn't abort the whole chain.
static PixelFX::PFXImage ApplyColourAdjustments(PixelFX::PFXImage p,
                                                const MediaAdjustments& a) {
    if (a.autoOptimize) {
        try { p = PixelFX::Colour::HistEqual(p); } catch (...) {}
    }
    if (a.gamma != 1.0) {
        try { p = PixelFX::Colour::Gamma(p, a.gamma); } catch (...) {}
    }
    if (a.brightness != 1.0) {
        try { p = PixelFX::Colour::Brightness(p, a.brightness); } catch (...) {}
    }
    if (a.red != 1.0 || a.green != 1.0 || a.blue != 1.0) {
        try {
            int bands = p.bands();
            std::vector<double> m;
            if (bands >= 3) {
                m = { a.red, a.green, a.blue };
                while (static_cast<int>(m.size()) < bands) m.push_back(1.0);
            } else {
                double avg = (a.red + a.green + a.blue) / 3.0;
                m.assign(bands > 0 ? bands : 1, avg);
            }
            p = PixelFX::Arithmetic::Multiply(p, m);
        } catch (...) {}
    }
    if (a.sharpen > 0.0) {
        try { p = PixelFX::Convolution::Sharpen(p, a.sharpen); } catch (...) {}
    }
    return p;
}
#endif // HAS_LIBVIPS

// ===========================================================================
// MEDIA SURFACE
// ===========================================================================

UltraCanvasMediaSurface::UltraCanvasMediaSurface(const std::string& elemId)
    : UltraCanvasUIElement(elemId, 0.0f, 0.0f, 0.0f, 0.0f) {
    SetMouseCursor(UCMouseCursor::Default);
    animator.onFrameChanged = [this]() { if (IsVisible()) RequestRedraw(); };
}

UltraCanvasMediaSurface::~UltraCanvasMediaSurface() {
    StopTransitionTimer();
}

double UltraCanvasMediaSurface::FitScale(double iw, double ih, int rotQ) const {
    Rect2Df b = GetLocalBounds();
    if (b.width <= 0 || b.height <= 0 || iw <= 0 || ih <= 0) return 0.0;
    bool swap = ((((rotQ % 4) + 4) % 4) % 2) == 1;   // 90° / 270° swap the box
    double effW = swap ? ih : iw;
    double effH = swap ? iw : ih;
    return std::min(b.width / effW, b.height / effH);
}

void UltraCanvasMediaSurface::RebuildProcessed() {
    processed.reset();
    if (!image || !image->IsValid()) return;
    if (adjust.IsIdentity()) return;   // nothing to do — draw the source directly
#ifdef HAS_LIBVIPS
    try {
        vips::VImage v = image->GetVImage();
        PixelFX::PFXImage p(v);
        p = ApplyColourAdjustments(p, adjust);
        processed = CreatePixmapFromVImage(p);
    } catch (...) {
        processed.reset();
    }
#endif
}

void UltraCanvasMediaSurface::SetAdjustments(const MediaAdjustments& adj) {
    adjust = adj;
    RebuildProcessed();
    // Adjustments are baked into one composited pixmap, so they freeze an
    // animated image on its current frame; identity resumes playback.
    if (!adjust.IsIdentity()) animator.Pause();
    else if (animator.GetAnimation()) animator.Play();
    RequestRedraw();
}

void UltraCanvasMediaSurface::ShowImage(std::shared_ptr<UCImage> img,
                                        MediaTransition transition,
                                        int durationMs, bool animated) {
    bool animate = animated && transition != MediaTransition::NoTransition &&
                   durationMs > 0 && image && image->IsValid();
    if (animate) {
        prevImage = image;
        prevProcessed = processed;
        prevRotQ = rotationQuarters;
        prevFlipH = flipH;
        prevFlipV = flipV;
        transitionStyle = transition;
        StartTransitionTimer(durationMs);
    } else {
        StopTransitionTimer();
        transitionActive = false;
        prevImage.reset();
        prevProcessed.reset();
    }

    image = std::move(img);
    // Each new image starts fit-to-window with no rotation/mirror.
    zoom = 1.0;
    panX = panY = 0.0;
    rotationQuarters = 0;
    flipH = flipV = false;
    RebuildProcessed();
    // Animated images (GIF / animated WebP) start playing right away unless
    // colour adjustments are active (those bake a single frame).
    animator.SetAnimation((image && image->IsValid() && image->IsAnimated())
                              ? image->GetAnimation() : nullptr);
    if (animator.GetAnimation() && adjust.IsIdentity()) animator.Play();
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

void UltraCanvasMediaSurface::ResetView() {
    zoom = 1.0;
    panX = panY = 0.0;
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

double UltraCanvasMediaSurface::GetZoomPercent() const {
    if (!image || !image->IsValid()) return 100.0;
    double fit = FitScale(image->GetWidth(), image->GetHeight(), rotationQuarters);
    return fit * zoom * 100.0;
}

bool UltraCanvasMediaSurface::IsAutoFit() const {
    return std::fabs(zoom - 1.0) < 0.001;
}

void UltraCanvasMediaSurface::SetZoomPercent(double percent) {
    if (!image || !image->IsValid()) return;
    double fit = FitScale(image->GetWidth(), image->GetHeight(), rotationQuarters);
    if (fit <= 0.0) return;
    zoom = std::max(0.02, std::min((percent / 100.0) / fit, 64.0));
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

void UltraCanvasMediaSurface::ZoomBy(double factor) {
    zoom = std::max(0.02, std::min(zoom * factor, 64.0));
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

void UltraCanvasMediaSurface::RotateBy(int quarters) {
    rotationQuarters = (((rotationQuarters + quarters) % 4) + 4) % 4;
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

void UltraCanvasMediaSurface::SetRotationQuarters(int q) {
    rotationQuarters = ((q % 4) + 4) % 4;
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

void UltraCanvasMediaSurface::ToggleFlipHorizontal() {
    flipH = !flipH;
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

void UltraCanvasMediaSurface::ToggleFlipVertical() {
    flipV = !flipV;
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

void UltraCanvasMediaSurface::ResetTransforms() {
    rotationQuarters = 0;
    flipH = flipV = false;
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

void UltraCanvasMediaSurface::StartTransitionTimer(int durationMs) {
    StopTransitionTimer();
    transitionActive = true;
    transitionProgress = 0.0;
    transitionDurationMs = std::max(1, durationMs);
    transitionStart = std::chrono::steady_clock::now();
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app) { transitionActive = false; return; }
    transitionTimer = app->StartTimer(16, true, [this](TimerId) {
        auto now = std::chrono::steady_clock::now();
        double el = std::chrono::duration<double, std::milli>(now - transitionStart).count();
        transitionProgress = el / static_cast<double>(transitionDurationMs);
        if (transitionProgress >= 1.0) {
            transitionProgress = 1.0;
            transitionActive = false;
            prevImage.reset();
            prevProcessed.reset();
            StopTransitionTimer();
        }
        RequestRedraw();
    });
}

void UltraCanvasMediaSurface::StopTransitionTimer() {
    if (transitionTimer) {
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(transitionTimer);
        transitionTimer = 0;
    }
}

void UltraCanvasMediaSurface::Blit(IRenderContext* ctx,
                                   const std::shared_ptr<UCImage>& img,
                                   const std::shared_ptr<UCPixmap>& pm,
                                   double iw, double ih, double scale,
                                   double cx, double cy, int rotQ,
                                   bool fH, bool fV, double alpha) {
    if (iw <= 0 || ih <= 0 || scale <= 0) return;
    double dispW = iw * scale;
    double dispH = ih * scale;
    ctx->PushState();
    if (alpha < 0.999) ctx->SetAlpha(alpha < 0.0 ? 0.0 : alpha);
    ctx->Translate(cx, cy);
    int q = ((rotQ % 4) + 4) % 4;
    if (q != 0) ctx->Rotate(q * kHalfPi);
    if (fH || fV) ctx->Scale(fH ? -1.0 : 1.0, fV ? -1.0 : 1.0);
    Rect2Dd dst(-dispW / 2.0, -dispH / 2.0, dispW, dispH);
    if (pm) {
        ctx->DrawPixmap(*pm, dst, ImageFitMode::Fill);
    } else if (img) {
        ctx->DrawImage(*img, dst, ImageFitMode::Fill);
    }
    ctx->PopState();
}

void UltraCanvasMediaSurface::DrawBackdrop(IRenderContext* ctx, const Rect2Df& b,
                                           double iw, double ih, double scale,
                                           double cx, double cy, int rotQ,
                                           double alpha) {
    if (iw <= 0 || ih <= 0 || scale <= 0 || alpha <= 0.001) return;
    // Displayed rectangle of the image in widget coordinates: rotation is in
    // 90° steps, so it stays axis-aligned (odd quarters swap the sides).
    bool swap = ((((rotQ % 4) + 4) % 4) % 2) == 1;
    double dispW = (swap ? ih : iw) * scale;
    double dispH = (swap ? iw : ih) * scale;
    double left = cx - dispW / 2.0;
    double top  = cy - dispH / 2.0;
    // Visible part only — at high zoom the image rectangle is far larger than
    // the widget, and the checkerboard cost scales with the area drawn.
    double x0 = std::max(left, static_cast<double>(b.x));
    double y0 = std::max(top,  static_cast<double>(b.y));
    double x1 = std::min(left + dispW, static_cast<double>(b.x) + b.width);
    double y1 = std::min(top  + dispH, static_cast<double>(b.y) + b.height);
    if (x1 <= x0 || y1 <= y0) return;

    ctx->PushState();
    if (alpha < 0.999) ctx->SetAlpha(alpha);
    if (transparentBackground == TransparentImageBackground::SolidColor) {
        ctx->DrawFilledRectangle(Rect2Dd(x0, y0, x1 - x0, y1 - y0),
                                 transparentColor, 0.0f);
    } else {
        constexpr double kCell = 12.0;                   // square size (px)
        const Color light(255, 255, 255, 255);
        const Color dark(203, 203, 203, 255);
        ctx->DrawFilledRectangle(Rect2Dd(x0, y0, x1 - x0, y1 - y0), light, 0.0f);
        // Pattern anchored to the image rectangle so it pans with the image.
        int c0 = static_cast<int>(std::floor((x0 - left) / kCell));
        int r0 = static_cast<int>(std::floor((y0 - top)  / kCell));
        for (int r = r0; top + r * kCell < y1; ++r) {
            double cy0 = std::max(top + r * kCell, y0);
            double cy1 = std::min(top + (r + 1) * kCell, y1);
            for (int c = c0 + ((c0 + r) % 2 == 0 ? 1 : 0); left + c * kCell < x1; c += 2) {
                double cx0 = std::max(left + c * kCell, x0);
                double cx1 = std::min(left + (c + 1) * kCell, x1);
                if (cx1 > cx0 && cy1 > cy0)
                    ctx->DrawFilledRectangle(Rect2Dd(cx0, cy0, cx1 - cx0, cy1 - cy0),
                                             dark, 0.0f);
            }
        }
    }
    ctx->PopState();
}

void UltraCanvasMediaSurface::DrawCurrent(IRenderContext* ctx, const Rect2Df& b) {
    if (!image || !image->IsValid()) {
        ctx->SetFontSize(16);
        ctx->SetTextPaint(Color(130, 130, 140, 255));
        Point2Dd p = ctx->CalculateCenteredTextPosition(
                "No media", Rect2Dd(b.x, b.y, b.width, b.height));
        ctx->DrawText("No media", p);
        return;
    }
    double iw = image->GetWidth();
    double ih = image->GetHeight();
    double fit = FitScale(iw, ih, rotationQuarters);
    double s = fit * zoom;
    double cx = b.width * 0.5 + panX;
    double cy = b.height * 0.5 + panY;
    // Colour-adjusted pixmap wins; otherwise an animated image draws its
    // current frame; a plain still falls through to the image itself.
    std::shared_ptr<UCPixmap> pm = processed ? processed : animator.GetCurrentFramePixmap();
    DrawBackdrop(ctx, b, iw, ih, s, cx, cy, rotationQuarters, 1.0);
    Blit(ctx, image, pm, iw, ih, s, cx, cy, rotationQuarters, flipH, flipV, 1.0);
}

void UltraCanvasMediaSurface::DrawInfoOverlay(IRenderContext* ctx, const Rect2Df& b) {
    if (infoText.empty()) return;
    double boxW = std::min(440.0, static_cast<double>(b.width) - 24.0);
    double boxH = std::min(280.0, static_cast<double>(b.height) - 24.0);
    if (boxW <= 40 || boxH <= 40) return;
    Rect2Dd box(b.x + 12, b.y + 12, boxW, boxH);
    ctx->DrawFilledRectangle(box, Color(0, 0, 0, 190), 1.0f, Color(255, 255, 255, 45), 8.0f);
    double pad = 14;
    ctx->SetFontSize(12);
    ctx->SetTextPaint(Color(235, 235, 240, 255));
    ctx->SetTextAlignment(TextAlignment::Left);
    ctx->SetTextWrap(TextWrap::WrapWord);
    ctx->DrawTextInRect(infoText,
            Rect2Dd(box.x + pad, box.y + pad, box.width - 2 * pad, box.height - 2 * pad));
}

void UltraCanvasMediaSurface::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
    if (!IsVisible()) return;
    Rect2Df b = GetLocalBounds();
    if (b.width <= 0 || b.height <= 0) return;

    ctx->PushState();
    ctx->ClipRect(Rect2Dd(b.x, b.y, b.width, b.height));
    ctx->DrawFilledRectangle(Rect2Dd(b.x, b.y, b.width, b.height), canvasColor, 0.0f);

    if (transitionActive && (prevImage || image)) {
        double t = std::max(0.0, std::min(transitionProgress, 1.0));
        double cx = b.width * 0.5;
        double cy = b.height * 0.5;

        double iwC = image ? image->GetWidth() : 0;
        double ihC = image ? image->GetHeight() : 0;
        double fitC = FitScale(iwC, ihC, rotationQuarters);

        double iwP = prevImage ? prevImage->GetWidth() : 0;
        double ihP = prevImage ? prevImage->GetHeight() : 0;
        double fitP = prevImage ? FitScale(iwP, ihP, prevRotQ) : 0;

        // Per-style opacity / offset / scale for the outgoing (prev) and
        // incoming (current) layers.
        double prevAlpha = 1.0 - t, curAlpha = t;
        double prevOffX = 0, prevOffY = 0, curOffX = 0, curOffY = 0;
        double prevScaleMul = 1.0, curScaleMul = 1.0;

        switch (transitionStyle) {
            case MediaTransition::CrossFade:
                break;
            case MediaTransition::FadeOutIn:
                if (t < 0.5) { prevAlpha = 1.0 - 2.0 * t; curAlpha = 0.0; }
                else         { prevAlpha = 0.0; curAlpha = 2.0 * t - 1.0; }
                break;
            case MediaTransition::SlideHorizontal:
                prevAlpha = curAlpha = 1.0;
                prevOffX = -b.width * t;
                curOffX  =  b.width * (1.0 - t);
                break;
            case MediaTransition::SlideVertical:
                prevAlpha = curAlpha = 1.0;
                prevOffY = -b.height * t;
                curOffY  =  b.height * (1.0 - t);
                break;
            case MediaTransition::ZoomFade:
                curScaleMul = 1.18 - 0.18 * t;   // settle from 1.18x to 1.0x
                break;
            default:
                prevAlpha = 0.0; curAlpha = 1.0;
                break;
        }

        if (prevImage && prevAlpha > 0.001) {
            DrawBackdrop(ctx, b, iwP, ihP, fitP * prevScaleMul,
                         cx + prevOffX, cy + prevOffY, prevRotQ, prevAlpha);
            Blit(ctx, prevImage, prevProcessed, iwP, ihP, fitP * prevScaleMul,
                 cx + prevOffX, cy + prevOffY, prevRotQ, prevFlipH, prevFlipV, prevAlpha);
        }
        if (image && curAlpha > 0.001) {
            std::shared_ptr<UCPixmap> curPm =
                    processed ? processed : animator.GetCurrentFramePixmap();
            DrawBackdrop(ctx, b, iwC, ihC, fitC * curScaleMul,
                         cx + curOffX, cy + curOffY, rotationQuarters, curAlpha);
            Blit(ctx, image, curPm, iwC, ihC, fitC * curScaleMul,
                 cx + curOffX, cy + curOffY, rotationQuarters, flipH, flipV, curAlpha);
        }
    } else {
        DrawCurrent(ctx, b);
    }

    if (showInfoPopup) DrawInfoOverlay(ctx, b);
    ctx->PopState();
}

bool UltraCanvasMediaSurface::HandleWheelZoom(const UCEvent& event) {
    if (!image || !image->IsValid()) return false;
    Rect2Df b = GetLocalBounds();
    double iw = std::max(1, image->GetWidth());
    double ih = std::max(1, image->GetHeight());
    double fit = FitScale(iw, ih, rotationQuarters);
    if (fit <= 0.0) return false;

    double step = (event.wheelDelta > 0) ? 1.15 : (1.0 / 1.15);
    double newZoom = std::max(0.05, std::min(zoom * step, 64.0));
    if (newZoom == zoom) return true;

    // Anchor the zoom under the cursor only when the image is un-rotated /
    // un-mirrored (the simple screen<->image mapping holds). Otherwise zoom
    // about the centre.
    if (rotationQuarters == 0 && !flipH && !flipV) {
        double sOld = fit * zoom;
        double leftOld = b.width * 0.5 + panX - iw * sOld * 0.5;
        double topOld  = b.height * 0.5 + panY - ih * sOld * 0.5;
        double imgX = (event.pointer.x - leftOld) / sOld;
        double imgY = (event.pointer.y - topOld)  / sOld;
        zoom = newZoom;
        double sNew = fit * zoom;
        double cxNew = event.pointer.x - imgX * sNew + iw * sNew * 0.5;
        double cyNew = event.pointer.y - imgY * sNew + ih * sNew * 0.5;
        panX = cxNew - b.width * 0.5;
        panY = cyNew - b.height * 0.5;
    } else {
        zoom = newZoom;
    }
    RequestRedraw();
    if (onViewChanged) onViewChanged();
    return true;
}

bool UltraCanvasMediaSurface::OnEvent(const UCEvent& event) {
    auto* app = UltraCanvasApplication::GetInstance();
    switch (event.type) {
        case UCEventType::MouseWheel:
            return HandleWheelZoom(event);

        case UCEventType::MouseDown:
            if (Contains(event.pointer)) {
                pressing = true;
                dragging = false;
                pressX = lastX = event.pointer.x;
                pressY = lastY = event.pointer.y;
                pressButton = event.button;
                SetFocus(true);
                if (app) app->CaptureMouse(this);
                if (showInfoPopup) { showInfoPopup = false; RequestRedraw(); }
                return true;
            }
            return false;

        case UCEventType::MouseMove:
            if (pressing) {
                double dx = event.pointer.x - pressX;
                double dy = event.pointer.y - pressY;
                if (!dragging && (dx * dx + dy * dy) > 16) dragging = true;
                if (dragging) {
                    panX += event.pointer.x - lastX;
                    panY += event.pointer.y - lastY;
                    lastX = event.pointer.x;
                    lastY = event.pointer.y;
                    RequestRedraw();
                }
                return true;
            }
            return false;

        case UCEventType::MouseUp:
            if (pressing) {
                pressing = false;
                if (app) app->ReleaseMouse();
                bool wasDrag = dragging;
                dragging = false;
                // A click (no drag) navigates: left = next, right = previous —
                // works whether or not the image is zoomed in.
                if (!wasDrag && onNavigate) {
                    if (pressButton == UCMouseButton::Left)       onNavigate(1);
                    else if (pressButton == UCMouseButton::Right)  onNavigate(-1);
                }
                return true;
            }
            return false;

        case UCEventType::MouseDoubleClick:
            ResetView();   // back to fit-to-window
            return true;

        case UCEventType::Drop:
            if (onFilesDropped && !event.droppedFiles.empty()) {
                onFilesDropped(event.droppedFiles);
                return true;
            }
            return false;

        case UCEventType::KeyDown: {
            UCKeys k = event.virtualKey;
            if (k == UCKeys::Left)  { if (onNavigate) onNavigate(-1); return true; }
            if (k == UCKeys::Right) { if (onNavigate) onNavigate(1);  return true; }
            if (k == UCKeys::Up)    { ZoomBy(1.15);       return true; }
            if (k == UCKeys::Down)  { ZoomBy(1.0 / 1.15); return true; }
            if (event.character == '+' || event.character == '=') { ZoomBy(1.15); return true; }
            if (event.character == '-') { ZoomBy(1.0 / 1.15); return true; }
            if (event.character == '0') { ResetView(); return true; }
            if (event.character == 'r' || event.character == 'R') { RotateBy(1); return true; }
            return false;
        }

        default:
            return false;
    }
}

bool UltraCanvasMediaSurface::SaveProcessed(const std::string& path, std::string& error) {
    if (!image || !image->IsValid()) { error = "No image to save"; return false; }
#ifdef HAS_LIBVIPS
    try {
        vips::VImage v = image->GetVImage();
        PixelFX::PFXImage p(v);
        p = ApplyColourAdjustments(p, adjust);
        int q = ((rotationQuarters % 4) + 4) % 4;
        if      (q == 1) p = PixelFX::Resample::Rot(p, PixelFX::Angle::D90);
        else if (q == 2) p = PixelFX::Resample::Rot(p, PixelFX::Angle::D180);
        else if (q == 3) p = PixelFX::Resample::Rot(p, PixelFX::Angle::D270);
        if (flipH) p = PixelFX::Resample::FlipHorizontal(p);
        if (flipV) p = PixelFX::Resample::FlipVertical(p);
        if (!PixelFX::FileIO::Save(p, path)) {
            error = PixelFX::GetLastError();
            if (error.empty()) error = "Save failed";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    } catch (...) {
        error = "Unknown error during save";
        return false;
    }
#else
    error = "Saving requires libvips support";
    return false;
#endif
}

// ===========================================================================
// MEDIA VIEWER
// ===========================================================================

UltraCanvasMediaViewer::UltraCanvasMediaViewer(const std::string& identifier,
                                               float x, float y, float w, float h)
    : UltraCanvasContainer(identifier, x, y, w, h) {
    BuildUI(w, h);
}

UltraCanvasMediaViewer::~UltraCanvasMediaViewer() {
    // The key filter captures `this`; it must not outlive the widget.
    RemoveKeyFilter();
    if (slideshowTimer) {
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(slideshowTimer);
        slideshowTimer = 0;
    }
    StopVideoClipTimer();
}

std::shared_ptr<UltraCanvasUIElement> UltraCanvasMediaViewer::BuildAdjustSlider(
        const std::string& id, const std::string& caption,
        float minV, float maxV, float value, std::function<void(float)> onChange) {
    auto box = std::make_shared<UltraCanvasContainer>(id + "_box", 0, 0, 124, 64);
    box->layout.SetFlexColumn().SetFlexGap(2)
               .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    box->layoutItem.SetFlexGrow(0).SetFlexShrink(0);

    auto lbl = std::make_shared<UltraCanvasLabel>(id + "_lbl", 0, 0, 124, 16, caption);
    lbl->SetFontSize(11);
    lbl->SetTextColor(Color(210, 210, 216, 255));
    lbl->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    box->AddChild(lbl);

    auto sld = std::make_shared<UltraCanvasSlider>(id, 0, 0, 124, 22);
    sld->SetRange(minV, maxV);
    sld->SetValue(value);
    sld->onValueChanged = [onChange](float v) { if (onChange) onChange(v); };
    sld->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    box->AddChild(sld);

    return box;
}

void UltraCanvasMediaViewer::BuildUI(float w, float h) {
    SetBackgroundColor(Color(24, 24, 28, 255));
    layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    // ----- FOLDER BREADCRUMB (top, Parallelogram style) -----
    // Built by BuildFolderBreadcrumb, the shared folder path mechanism (see
    // UpdateBreadcrumb): "Computer" + drive + one node per folder. Each segment
    // navigates to that folder; its dropdown lists the folders inside that
    // segment so the path can be extended one level. Long paths collapse their
    // middle segments into a "..." overflow menu, keeping the root and the last
    // two folders visible.
    breadcrumb = std::make_shared<UltraCanvasBreadcrumb>("MV_Breadcrumb", 0, 0, 0, 30);
    {
        BreadcrumbStyle bs = BreadcrumbStyle::Parallelogram();
        bs.overflowMode = BreadcrumbOverflowMode::Collapse;  // collapse middle into "..."
        bs.keepFirstItemOnCollapse = true;
        bs.minVisibleAfterCollapse = 2;
        // Compact segment metrics (as on the filer's path row) so a deep path
        // keeps as many folders as possible before collapsing.
        bs.arrowSize = 8;
        bs.itemPaddingHorizontal = 9;
        bs.itemPaddingVertical = 3;
        bs.fontStyle.fontSize = 11.0f;
        bs.backgroundColor = Color(30, 30, 36, 255);
        breadcrumb->SetStyle(bs);
    }
    breadcrumb->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                          .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    breadcrumb->SetVisible(false);   // shown once a folder is known
    AddChild(breadcrumb);

    // ----- TOP TOOLBAR ROW 1: navigation + slideshow -----
    // Two rows keep every control visible at modest widths (the toolbar widget
    // does not yet implement overflow handling).
    toolbar = std::make_shared<UltraCanvasToolbar>("MV_Toolbar", 0, 0, 0, 40);
    toolbar->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                       .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    toolbar->AddButton("mv_open", "Open", "", [this] { ShowOpenDialog(); });
    toolbar->AddSeparator("mv_sep0");
    toolbar->AddButton("mv_prev", "Prev", "", [this] { Previous(); });
    toolbar->AddButton("mv_next", "Next", "", [this] { Next(); });
    toolbar->AddSeparator("mv_sep1");
    playButton = toolbar->AddToggleButton("mv_play", "Slideshow", "",
            [this](bool on) { if (on) PlaySlideshow(); else PauseSlideshow(); });
    toolbar->AddDropdownButton("mv_interval", "Interval",
            { "3 s", "5 s", "7 s", "10 s" },
            [this](const std::string& s) {
                double sec = std::atof(s.c_str());
                if (sec > 0) SetSlideshowIntervalSeconds(sec);
            });
    toolbar->AddDropdownButton("mv_trans", "Transition",
            { "None", "Cross fade", "Fade out/in", "Slide H", "Slide V", "Zoom" },
            [this](const std::string& s) {
                MediaTransition t = MediaTransition::CrossFade;
                if      (s == "None")        t = MediaTransition::NoTransition;
                else if (s == "Cross fade")  t = MediaTransition::CrossFade;
                else if (s == "Fade out/in") t = MediaTransition::FadeOutIn;
                else if (s == "Slide H")     t = MediaTransition::SlideHorizontal;
                else if (s == "Slide V")     t = MediaTransition::SlideVertical;
                else if (s == "Zoom")        t = MediaTransition::ZoomFade;
                SetTransition(t);
            });
    AddChild(toolbar);

    // ----- TOP TOOLBAR ROW 2: view + edit -----
    toolbar2 = std::make_shared<UltraCanvasToolbar>("MV_Toolbar2", 0, 0, 0, 40);
    toolbar2->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    toolbar2->AddButton("mv_zoomout", "Zoom -", "", [this] { ZoomOutAction(); });
    toolbar2->AddDropdownButton("mv_zoom", "Zoom",
            { "Fit", "25%", "50%", "75%", "100%", "150%", "200%", "400%" },
            [this](const std::string& s) {
                if (s == "Fit") ZoomFitAction();
                else            ZoomPercentAction(std::atof(s.c_str()));
            });
    toolbar2->AddButton("mv_zoomin", "Zoom +", "", [this] { ZoomInAction(); });
    toolbar2->AddButton("mv_fit", "Fit", "", [this] { ZoomFitAction(); });
    toolbar2->AddSeparator("mv_sep3");
    toolbar2->AddButton("mv_rotl", "Rotate L", "", [this] { if (surface) surface->RotateBy(-1); });
    toolbar2->AddButton("mv_rotr", "Rotate R", "", [this] { if (surface) surface->RotateBy(1); });
    toolbar2->AddButton("mv_mirh", "Mirror H", "", [this] { if (surface) surface->ToggleFlipHorizontal(); });
    toolbar2->AddButton("mv_mirv", "Mirror V", "", [this] { if (surface) surface->ToggleFlipVertical(); });
    toolbar2->AddSeparator("mv_sep4");
    toolbar2->AddToggleButton("mv_adjust", "Adjust", "",
            [this](bool on) { if (adjustPanel) adjustPanel->SetVisible(on); });
    toolbar2->AddButton("mv_save", "Save as", "", [this] { ShowSaveDialog(); });
    toolbar2->AddButton("mv_info", "Info", "", [this] { if (surface) surface->ToggleInfoPopup(); });
    AddChild(toolbar2);

    // ----- ADJUSTMENTS PANEL (hidden until "Adjust" is toggled) -----
    adjustPanel = std::make_shared<UltraCanvasContainer>("MV_Adjust", 0, 0, 0, 84);
    adjustPanel->SetBackgroundColor(Color(34, 34, 40, 255));
    adjustPanel->layout.SetFlexRow().SetFlexGap(10)
                       .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    adjustPanel->SetPadding(8, 12, 8, 12);
    adjustPanel->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    adjustPanel->SetVisible(false);

    adjustPanel->AddChild(BuildAdjustSlider("adj_gamma", "Gamma", 0.2f, 3.0f, 1.0f,
            [this](float v) { adjustments.gamma = v; ApplyAdjustments(); }));
    adjustPanel->AddChild(BuildAdjustSlider("adj_bright", "Brightness", 0.2f, 2.5f, 1.0f,
            [this](float v) { adjustments.brightness = v; ApplyAdjustments(); }));
    adjustPanel->AddChild(BuildAdjustSlider("adj_red", "Red", 0.0f, 2.0f, 1.0f,
            [this](float v) { adjustments.red = v; ApplyAdjustments(); }));
    adjustPanel->AddChild(BuildAdjustSlider("adj_green", "Green", 0.0f, 2.0f, 1.0f,
            [this](float v) { adjustments.green = v; ApplyAdjustments(); }));
    adjustPanel->AddChild(BuildAdjustSlider("adj_blue", "Blue", 0.0f, 2.0f, 1.0f,
            [this](float v) { adjustments.blue = v; ApplyAdjustments(); }));
    adjustPanel->AddChild(BuildAdjustSlider("adj_sharp", "Sharpen", 0.0f, 3.0f, 0.0f,
            [this](float v) { adjustments.sharpen = v; ApplyAdjustments(); }));

    auto autoBtn = std::make_shared<UltraCanvasButton>("adj_auto", 0, 0, 96, 28, "Auto");
    autoBtn->onClick = [this] {
        adjustments.autoOptimize = !adjustments.autoOptimize;
        ApplyAdjustments();
    };
    autoBtn->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    adjustPanel->AddChild(autoBtn);

    auto resetBtn = std::make_shared<UltraCanvasButton>("adj_reset", 0, 0, 96, 28, "Reset");
    resetBtn->onClick = [this] {
        adjustments = MediaAdjustments();
        ApplyAdjustments();
    };
    resetBtn->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    adjustPanel->AddChild(resetBtn);
    AddChild(adjustPanel);

    // ----- CENTRAL IMAGE SURFACE -----
    surface = std::make_shared<UltraCanvasMediaSurface>("MV_Surface");
    surface->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                       .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    surface->onNavigate = [this](int d) { if (d < 0) Previous(); else Next(); };
    surface->onViewChanged = [this] { UpdateInfoBar(); };
    surface->onFilesDropped = [this](const std::vector<std::string>& files) {
        HandleDroppedFiles(files);
    };
    surface->SetAdjustments(adjustments);
    AddChild(surface);

#ifdef ULTRACANVAS_PLUGIN_PDF
    // ----- PDF DOCUMENT VIEW (shown instead of the image surface for PDFs) -----
    // Built with a zero size so the flex layout (not a fixed CSS size) drives it.
    {
        auto pv = std::make_shared<UltraCanvasPDFView>("MV_PDFView", 0, 0, 0, 0);
        pv->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        pv->onPageChanged = [this](int, int) { UpdateInfoBar(); };
        // The page zooms with the wheel and the keyboard, so the info bar
        // reports the zoom the way it does for images.
        pv->onZoomChanged = [this](float) { UpdateInfoBar(); };
        // Page numbers drawn over the thumbnail pages (not captions beneath).
        pv->SetThumbnailNumberStyle(
            UltraCanvasPDFView::ThumbnailNumberStyle::Overlay);
        pv->SetVisible(false);
        pdfView = pv;
        AddChild(pdfView);
        // Page-inventory width and wheel zoom as configured on the viewer.
        ApplyPDFViewSettings();
    }
#endif

    // ----- SPREADSHEET VIEW (shown for ODS / CSV / TSV) -----
    {
        auto sv = std::make_shared<UltraCanvasSpreadsheet>("MV_Sheet", 0, 0, 0, 0);
        sv->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        sv->SetVisible(false);
        sheetView = sv;
        AddChild(sheetView);
    }

    // ----- 3D MODEL VIEW (shown for STL files) -----
    {
        auto mv = std::make_shared<UltraCanvasSTLElement>("MV_Model", 0, 0, 0, 0);
        mv->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        mv->onLoadError = [this](const std::string& err) {
            if (infoLabel) infoLabel->SetText("Failed to open 3D model: " + err);
        };
        mv->SetVisible(false);
        modelView = mv;
        AddChild(modelView);
    }

    // ----- TEXT / SOURCE / MARKDOWN VIEW (display only) -----
    // Display-only, not merely read-only: the area stays out of the focus chain,
    // so it shows no caret and never captures Left/Right — those keep browsing
    // the folder. The viewer scrolls it from HandleViewerKey() instead.
    {
        auto tv = std::make_shared<UltraCanvasTextArea>("MV_Text", 0, 0, 0, 0);
        tv->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        tv->SetDisplayOnly(true);
        tv->SetVisible(false);
        textView = tv;
        AddChild(textView);
    }

    // ----- EBOOK VIEW (EPUB / FB2 / MOBI / AZW) -----
    // Complete reading widget: chapter toolbar, table of contents and reflowing
    // chapter content. Engines come from the eBook registry; registering the
    // built-ins here is idempotent, so the viewer works standalone too.
    {
        RegisterBuiltinEBookEngines();
        auto bv = std::make_shared<UltraCanvasEBookViewer>("MV_Book", 0.f, 0.f);
        bv->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        bv->SetVisible(false);
        bookView = bv;
        AddChild(bookView);
    }

#ifdef ULTRACANVAS_ENABLE_VIDEO
    // ----- VIDEO PLAYER (shown for video files) -----
    {
        auto vp = std::make_shared<UltraCanvasVideoPlayerElement>("MV_Video", 0, 0, 0, 0);
        vp->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        // Auto-advance to the next file when a clip ends during a slideshow.
        vp->onEnded = [this] { if (slideshowPlaying) Next(); };
        // A finished PreviewClip is left paused *and muted*; the sound comes
        // back the moment the user resumes it from the transport bar, which is
        // the only point where the pipeline is certain to be playing again.
        vp->onPlay = [this] {
            if (!videoClipUnmutePending) return;
            videoClipUnmutePending = false;
            if (!videoPlayer) return;
            auto* v = static_cast<UltraCanvasVideoPlayerElement*>(videoPlayer.get());
            if (auto p = v->GetPlayer()) p->SetMute(false);
        };
        vp->SetVisible(false);
        videoPlayer = vp;
        AddChild(videoPlayer);
    }
#endif

#ifdef ULTRACANVAS_ENABLE_AUDIO
    // ----- AUDIO PLAYER (shown for audio files) -----
    {
        auto ap = std::make_shared<UltraCanvasAudioPlayerElement>("MV_Audio", 0, 0, 0, 0);
        ap->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        ap->onEnded = [this] { if (slideshowPlaying) Next(); };
        ap->SetVisible(false);
        audioPlayer = ap;
        AddChild(audioPlayer);
    }
#endif

    // ----- BACKDROP PALETTE (directly under the picture) -----
    // Only up for a file that really has transparency (see
    // UpdateTransparencyPalette): the checkered swatch first, then greys and
    // colours. The strip sizes its own swatches to the width it gets, so it
    // fits a narrow preview pane as well as a full window.
    backdropBar = CreateBackdropSwatchBar("MV_Backdrop", 0, 0, 0, 28);
    {
        ColorSwatchBarStyle bs = backdropBar->GetStyle();
        bs.background     = Color(30, 30, 36, 255);
        bs.border         = Color(70, 70, 78, 255);
        bs.hoverBorder    = Color(210, 210, 218, 255);
        bs.selectedBorder = Color(90, 160, 240, 255);
        backdropBar->SetStyle(bs);
    }
    backdropBar->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    backdropBar->SetVisible(false);
    backdropBar->onColorSelected = [this](const Color& c) {
        SetTransparentBackground(TransparentImageBackground::SolidColor);
        SetTransparentColor(c);
        if (onTransparentBackgroundChanged) {
            onTransparentBackgroundChanged(TransparentImageBackground::SolidColor, c);
        }
    };
    backdropBar->onCheckeredSelected = [this]() {
        SetTransparentBackground(TransparentImageBackground::Checkered);
        if (onTransparentBackgroundChanged) {
            onTransparentBackgroundChanged(TransparentImageBackground::Checkered,
                                           GetTransparentColor());
        }
    };
    AddChild(backdropBar);

    // ----- BOTTOM INFO BAR -----
    bottomBar = std::make_shared<UltraCanvasContainer>("MV_Bottom", 0, 0, 0, 26);
    bottomBar->SetBackgroundColor(Color(18, 18, 22, 255));
    bottomBar->layout.SetFlexRow().SetFlexGap(8)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    bottomBar->SetPadding(2, 10, 2, 10);
    bottomBar->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                         .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    infoLabel = std::make_shared<UltraCanvasLabel>("MV_InfoLabel", 0, 0, 0, 20, "No media");
    infoLabel->SetFontSize(11);
    infoLabel->SetTextColor(Color(200, 200, 206, 255));
    infoLabel->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    bottomBar->AddChild(infoLabel);

    auto detailsBtn = std::make_shared<UltraCanvasButton>("MV_Details", 0, 0, 72, 20, "Details");
    detailsBtn->onClick = [this] { if (surface) surface->ToggleInfoPopup(); };
    detailsBtn->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    bottomBar->AddChild(detailsBtn);
    AddChild(bottomBar);

    (void)w; (void)h;
}

bool UltraCanvasMediaViewer::IsDocumentFile(const std::string& path) {
    // Documents rendered through a dedicated document element rather than the
    // image pipeline. PDF is handled by UltraCanvasPDFView (MuPDF) and is only
    // advertised as supported when that plugin is compiled in.
#ifdef ULTRACANVAS_PLUGIN_PDF
    return LowerExt(path) == "pdf";
#else
    (void)path;
    return false;
#endif
}

bool UltraCanvasMediaViewer::IsSpreadsheetFile(const std::string& path) {
    // Spreadsheets open in UltraCanvasSpreadsheet (ODS / CSV / TSV). The engine
    // is always compiled into the core library, so no backend guard is needed.
    // (ODT is an OpenDocument *text* document, not a spreadsheet — not handled.)
    std::string e = LowerExt(path);
    return e == "ods" || e == "csv" || e == "tsv";
}

bool UltraCanvasMediaViewer::IsModelFile(const std::string& path) {
    // 3D models open in UltraCanvasSTLElement (OpenGL viewer, or a 2D info
    // placeholder when GL is disabled). The element is always built into the
    // core library, so no backend guard is needed.
    return LowerExt(path) == "stl";
}

bool UltraCanvasMediaViewer::IsEBookFile(const std::string& path) {
    // e-books open in UltraCanvasEBookViewer through the engine registry
    // (EPUB / FB2 / MOBI and Kindle variants). Plain text stays in the text
    // view even though a TXT e-book engine exists, so only the dedicated
    // e-book container formats are claimed here.
    static const std::vector<std::string> b = {
        "epub", "fb2", "mobi", "prc", "azw", "azw3"
    };
    std::string e = LowerExt(path);
    return !e.empty() && std::find(b.begin(), b.end(), e) != b.end();
}

bool UltraCanvasMediaViewer::IsUCDFile(const std::string& path) {
    // UltraCanvas Document containers. Shown as the embedded preview thumbnail
    // plus header details until the UCD v2 engine lands (see LoadCurrent).
    return LowerExt(path) == "ucd";
}

// Image / vector formats the image pipeline can rasterize. Kept in one place
// because both ClassifyFile() and IsSupportedMedia() need the same list.
static const std::vector<std::string>& ImageExtensions() {
    static const std::vector<std::string> exts = {
        "jpg", "jpeg", "jpe", "png", "gif", "bmp", "webp", "tif", "tiff",
        "svg", "svgz", "ico", "cur", "heic", "heif", "avif", "jxl", "jp2", "j2k",
        "ppm", "pgm", "pbm", "pnm", "pfm", "tga", "psd", "qoi", "hdr", "exr",
        "xpm", "xbm", "pcx", "sgi", "dds", "fits"
    };
    return exts;
}

bool UltraCanvasMediaViewer::IsImageFile(const std::string& path) {
    std::string e = LowerExt(path);
    if (e.empty()) return false;
    const auto& exts = ImageExtensions();
    return std::find(exts.begin(), exts.end(), e) != exts.end();
}

bool UltraCanvasMediaViewer::IsTextFile(const std::string& path) {
    // Text / markup / source files open in a read-only UltraCanvasTextArea. A
    // curated set of plain-text & markup extensions, plus any source language
    // the syntax tokenizer recognises (so highlighting matches the editor).
    std::string e = LowerExt(path);
    if (e.empty()) return false;
    static const std::vector<std::string> textExts = {
        "txt", "text", "log", "md", "markdown", "rst", "json", "xml",
        "yaml", "yml", "ini", "cfg", "conf", "toml", "html", "htm", "css",
        "tex", "srt", "vtt", "diff", "patch"
    };
    if (std::find(textExts.begin(), textExts.end(), e) != textExts.end()) return true;
    // Reuse the syntax tokenizer's language registry for source code (cpp, py,
    // js, java, …). Constructed once; mutation of its current-language state is
    // harmless here since we only need the match result.
    static SyntaxTokenizer tokenizer;
    return tokenizer.SetLanguageByExtension(e);
}

bool UltraCanvasMediaViewer::IsVideoFile(const std::string& path) {
    // Video plays through UltraCanvasVideoPlayerElement; only advertised when a
    // real video backend is compiled in.
#ifdef ULTRACANVAS_ENABLE_VIDEO
    static const std::vector<std::string> v = {
        "mp4", "m4v", "mkv", "webm", "mov", "avi", "wmv",
        "flv", "mpg", "mpeg", "ogv", "3gp", "ts"
    };
    std::string e = LowerExt(path);
    return !e.empty() && std::find(v.begin(), v.end(), e) != v.end();
#else
    (void)path;
    return false;
#endif
}

bool UltraCanvasMediaViewer::IsAudioFile(const std::string& path) {
    // Audio plays through UltraCanvasAudioPlayerElement; only advertised when a
    // real audio backend is compiled in.
#ifdef ULTRACANVAS_ENABLE_AUDIO
    static const std::vector<std::string> a = {
        "mp3", "wav", "flac", "ogg", "oga", "m4a",
        "aac", "opus", "wma", "aif", "aiff"
    };
    std::string e = LowerExt(path);
    return !e.empty() && std::find(a.begin(), a.end(), e) != a.end();
#else
    (void)path;
    return false;
#endif
}

MediaKind UltraCanvasMediaViewer::ClassifyFile(const std::string& path) {
    if (IsUCDFile(path))         return MediaKind::UCDoc;
    if (IsDocumentFile(path))    return MediaKind::Document;
    if (IsSpreadsheetFile(path)) return MediaKind::Sheet;
    if (IsModelFile(path))       return MediaKind::Model;
    if (IsEBookFile(path))       return MediaKind::Book;
    if (IsVideoFile(path))       return MediaKind::Video;
    if (IsAudioFile(path))       return MediaKind::Audio;
    // Images before text: SVG (and XPM / XBM) are markup the syntax tokenizer
    // recognises, so classifying by text first would preview their source code
    // instead of the picture they describe.
    if (IsImageFile(path))       return MediaKind::Image;
    if (IsTextFile(path))        return MediaKind::Text;
    return MediaKind::Image;
}

bool UltraCanvasMediaViewer::IsSupportedMedia(const std::string& path) {
    std::string e = LowerExt(path);
    if (e.empty()) return false;
    if (IsImageFile(path)) return true;
    // Documents / spreadsheets / 3D models / e-books / UCD containers / text /
    // video / audio (video & audio gated by their backend being present).
    return IsDocumentFile(path) || IsSpreadsheetFile(path) || IsModelFile(path) ||
           IsEBookFile(path) || IsUCDFile(path) ||
           IsVideoFile(path) || IsAudioFile(path) || IsTextFile(path);
}

std::vector<std::string> UltraCanvasMediaViewer::EnumerateFolder(const std::string& folder) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) return out;
    for (fs::directory_iterator it(folder, ec), end; it != end && !ec; it.increment(ec)) {
        std::error_code fec;
        if (it->is_regular_file(fec)) {
            std::string p = it->path().string();
            if (IsSupportedMedia(p)) out.push_back(p);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string UltraCanvasMediaViewer::GetCurrentPath() const {
    if (currentIndex < playlist.size()) return playlist[currentIndex];
    return "";
}

void UltraCanvasMediaViewer::OpenFolder(const std::string& folderPath,
                                        const std::string& selectFile) {
    playlist = EnumerateFolder(folderPath);
    currentIndex = 0;
    if (!selectFile.empty()) {
        std::string target = BaseName(selectFile);
        for (size_t i = 0; i < playlist.size(); ++i) {
            if (playlist[i] == selectFile || BaseName(playlist[i]) == target) {
                currentIndex = i;
                break;
            }
        }
    }
    currentFolder = folderPath;
    UpdateBreadcrumb();
    LoadCurrent(false);
    if (slideshowPlaying) { PauseSlideshow(); PlaySlideshow(); }
}

void UltraCanvasMediaViewer::SetFiles(const std::vector<std::string>& files, size_t startIndex) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& f : files) {
        if (fs::is_directory(f, ec)) {
            auto sub = EnumerateFolder(f);
            out.insert(out.end(), sub.begin(), sub.end());
        } else if (IsSupportedMedia(f)) {
            out.push_back(f);
        }
    }
    if (out.empty()) return;
    playlist = std::move(out);
    currentIndex = std::min(startIndex, playlist.size() - 1);
    // Reflect the folder the (first) file lives in.
    {
        fs::path p(playlist.front());
        currentFolder = p.parent_path().string();
    }
    UpdateBreadcrumb();
    LoadCurrent(false);
    if (slideshowPlaying) { PauseSlideshow(); PlaySlideshow(); }
}

void UltraCanvasMediaViewer::OpenFile(const std::string& filePath) {
    std::error_code ec;
    fs::path p(filePath);
    std::string folder = p.parent_path().string();
    if (folder.empty()) folder = ".";
    OpenFolder(folder, filePath);
}

// Show nothing and let go of the file. Every backend that can keep an operating
// system handle on the shown file is released here: on Windows a document engine
// that still has the file open makes a move / rename of it fail, which is exactly
// what a preview pane hosting this viewer runs into.
void UltraCanvasMediaViewer::CloseFile() {
    StopPlayback();
    ReleaseViewBackends();
    playlist.clear();
    currentIndex = 0;
    currentFolder.clear();
    UpdateBreadcrumb();
    LoadCurrent(false);   // empty playlist: clears the surface and the info bar
}

void UltraCanvasMediaViewer::ReleaseViewBackends() {
#ifdef ULTRACANVAS_PLUGIN_PDF
    // MuPDF keeps the PDF open for as long as the document object lives.
    if (pdfView) static_cast<UltraCanvasPDFView*>(pdfView.get())->SetDocument(nullptr);
#endif
    if (bookView) static_cast<UltraCanvasEBookViewer*>(bookView.get())->CloseDocument();
    if (textView) static_cast<UltraCanvasTextArea*>(textView.get())->SetText("");
    if (surface) surface->ShowImage(nullptr, MediaTransition::NoTransition, 0, false);
    ucdDetails.clear();
    // Spreadsheets and 3D models are parsed into memory by their loaders, and
    // the video / audio backends expose no release beyond Stop() (done above).
}

void UltraCanvasMediaViewer::ShowOpenDialog() {
    FileDialogOptions opts;
    opts.SetTitle("Open media")
        .AddFilter("Images", std::vector<std::string>{
            "png", "jpg", "jpeg", "gif", "bmp", "webp", "tiff", "tif",
            "svg", "ico", "heic", "heif", "avif", "jxl", "tga", "ppm", "qoi" })
        .AddFilter("All files", std::vector<std::string>{ "*" })
        .SetParentWindow(GetWindow());
    UltraCanvasFileLoader::OpenMultipleFilesDialog(opts,
            [this](DialogResult r, const std::vector<std::string>& files) {
                if (r != DialogResult::OK || files.empty()) return;
                // One picked file browses its whole folder (arrow keys / slideshow
                // then have the siblings); a multi-selection is the playlist.
                if (files.size() == 1) OpenFile(files[0]);
                else                   SetFiles(files, 0);
            });
}

void UltraCanvasMediaViewer::ShowSaveDialog() {
    if (!surface || !surface->GetImage() || !surface->GetImage()->IsValid()) return;
    std::string current = GetCurrentPath();
    std::string defName = current.empty() ? "image.png" : BaseName(current);

    FileDialogOptions opts;
    opts.SetTitle("Save image as")
        .SetDefaultFileName(defName)
        .AddFilter("PNG image",  std::vector<std::string>{ "png" })
        .AddFilter("JPEG image", std::vector<std::string>{ "jpg", "jpeg" })
        .AddFilter("WebP image", std::vector<std::string>{ "webp" })
        .AddFilter("TIFF image", std::vector<std::string>{ "tiff", "tif" })
        .AddFilter("AVIF image", std::vector<std::string>{ "avif" })
        .AddFilter("BMP image",  std::vector<std::string>{ "bmp" })
        .SetParentWindow(GetWindow());
    UltraCanvasFileLoader::SaveFileDialog(opts,
            [this](DialogResult r, const std::string& path) {
                if (r != DialogResult::OK || path.empty() || !surface) return;
                std::string err;
                if (surface->SaveProcessed(path, err)) {
                    if (infoLabel) infoLabel->SetText("Saved: " + BaseName(path));
                } else if (infoLabel) {
                    infoLabel->SetText("Save failed: " + err);
                }
            });
}

void UltraCanvasMediaViewer::Next() {
    if (playlist.empty()) return;
    currentIndex = (currentIndex + 1) % playlist.size();
    LoadCurrent(true);
}

void UltraCanvasMediaViewer::Previous() {
    if (playlist.empty()) return;
    currentIndex = (currentIndex + playlist.size() - 1) % playlist.size();
    LoadCurrent(true);
}

void UltraCanvasMediaViewer::GoTo(size_t index, bool animated) {
    if (index >= playlist.size()) return;
    currentIndex = index;
    LoadCurrent(animated);
}

void UltraCanvasMediaViewer::ShowView(MediaKind kind) {
    activeKind = kind;
    if (surface)     surface->SetVisible(kind == MediaKind::Image);
    if (pdfView)     pdfView->SetVisible(kind == MediaKind::Document);
    if (sheetView)   sheetView->SetVisible(kind == MediaKind::Sheet);
    if (modelView)   modelView->SetVisible(kind == MediaKind::Model);
    if (textView)    textView->SetVisible(kind == MediaKind::Text);
    if (bookView)    bookView->SetVisible(kind == MediaKind::Book);
    if (videoPlayer) videoPlayer->SetVisible(kind == MediaKind::Video);
    if (audioPlayer) audioPlayer->SetVisible(kind == MediaKind::Audio);
}

void UltraCanvasMediaViewer::SetTopBarsVisible(bool visible) {
    if (topBarsVisible == visible) return;
    topBarsVisible = visible;
    if (toolbar)  toolbar->SetVisible(visible);
    if (toolbar2) toolbar2->SetVisible(visible);
    // The adjustments panel opens through its toolbar2 toggle; it never stays
    // open (or reappears) while the bars are hidden.
    if (!visible && adjustPanel) adjustPanel->SetVisible(false);
    UpdateBreadcrumb();
    RequestRedraw();
}

void UltraCanvasMediaViewer::SetTransparentBackground(TransparentImageBackground mode) {
    if (surface) surface->SetTransparentBackground(mode);
    SyncBackdropSelection();
}

TransparentImageBackground UltraCanvasMediaViewer::GetTransparentBackground() const {
    return surface ? surface->GetTransparentBackground()
                   : TransparentImageBackground::SolidColor;
}

void UltraCanvasMediaViewer::SetTransparentColor(const Color& c) {
    if (surface) surface->SetTransparentColor(c);
    SyncBackdropSelection();
}

void UltraCanvasMediaViewer::SetTransparencyPaletteVisible(bool visible) {
    transparencyPaletteEnabled = visible;
    UpdateTransparencyPalette();
}

// ===== BACKDROP PALETTE =====
// The strip is only up while it means something: the shown file is an image
// (the PDF, sheet, text, … views paint their own background) and that image
// really has transparency — an alpha channel that is used, or a vector
// document. Everything else would be a row of colours changing nothing.

void UltraCanvasMediaViewer::SyncBackdropSelection() {
    if (!backdropBar || !surface) return;
    if (surface->GetTransparentBackground() == TransparentImageBackground::Checkered) {
        backdropBar->SelectCheckered();
    } else {
        // A colour the palette does not hold (one picked in a settings dialog)
        // simply leaves no swatch marked.
        backdropBar->SelectColor(surface->GetTransparentColor());
    }
}

void UltraCanvasMediaViewer::UpdateTransparencyPalette() {
    if (!backdropBar) return;
    bool show = transparencyPaletteEnabled && activeKind == MediaKind::Image &&
                surface != nullptr;
    if (show) {
        auto img = surface->GetImage();
        show = img && img->IsValid() && img->HasTransparency();
    }
    if (show != backdropBar->IsVisible()) {
        backdropBar->SetVisible(show);
        RequestRedraw();
    }
    if (show) SyncBackdropSelection();
}

Color UltraCanvasMediaViewer::GetTransparentColor() const {
    return surface ? surface->GetTransparentColor() : Color(255, 255, 255, 255);
}

void UltraCanvasMediaViewer::UpdateBreadcrumb() {
    if (!breadcrumb) return;
    if (currentFolder.empty() || !topBarsVisible) {
        breadcrumb->Clear();
        breadcrumb->SetVisible(false);
        return;
    }

    // Same path mechanism the Filer uses: a leading "Computer" node listing the
    // drives / volumes, the drive (or root) node, then one node per folder — the
    // path separator never becomes a node of its own. Clicking a node (or an
    // entry of its sub-folder dropdown) browses that folder here.
    BuildFolderBreadcrumb(breadcrumb.get(), currentFolder,
                          [this](const std::string& folder) { OpenFolder(folder); });
    breadcrumb->SetVisible(true);
}

void UltraCanvasMediaViewer::LoadCurrent(bool animated) {
    if (!surface) return;

    // Stop any media that was playing before we switch away from it.
    StopVideoClipTimer();
#ifdef ULTRACANVAS_ENABLE_VIDEO
    if (videoPlayer) static_cast<UltraCanvasVideoPlayerElement*>(videoPlayer.get())->Stop();
#endif
#ifdef ULTRACANVAS_ENABLE_AUDIO
    if (audioPlayer) static_cast<UltraCanvasAudioPlayerElement*>(audioPlayer.get())->Stop();
#endif

    if (playlist.empty()) {
        ShowView(MediaKind::Image);
        surface->ShowImage(nullptr, MediaTransition::NoTransition, 0, false);
        UpdateTransparencyPalette();   // nothing shown - the strip goes away
        UpdateInfoBar();
        return;
    }
    if (currentIndex >= playlist.size()) currentIndex = playlist.size() - 1;
    const std::string& path = playlist[currentIndex];

    MediaKind kind = ClassifyFile(path);
    (void)kind;
    bool handled = false;
    ucdDetails.clear();   // only set while a *.ucd container is showing

#ifdef ULTRACANVAS_PLUGIN_PDF
    if (kind == MediaKind::Document && pdfView) {
        // Render the document through the dedicated PDF element (MuPDF), not the
        // image/libvips path. Drop the now-hidden image so it frees memory.
        ShowView(MediaKind::Document);
        surface->ShowImage(nullptr, MediaTransition::NoTransition, 0, false);
        auto* pv = static_cast<UltraCanvasPDFView*>(pdfView.get());
        if (!pv->LoadFromPath(path) && infoLabel)
            infoLabel->SetText("Failed to open document: " + BaseName(path));
        handled = true;
    }
#endif
    if (!handled && kind == MediaKind::Sheet && sheetView) {
        // Spreadsheets (ODS / CSV / TSV) open in the spreadsheet engine.
        ShowView(MediaKind::Sheet);
        surface->ShowImage(nullptr, MediaTransition::NoTransition, 0, false);
        auto* sv = static_cast<UltraCanvasSpreadsheet*>(sheetView.get());
        if (!sv->LoadFromFile(path) && infoLabel)
            infoLabel->SetText("Failed to open spreadsheet: " + BaseName(path) +
                               " (" + sv->GetLastError() + ")");
        handled = true;
    }
    if (!handled && kind == MediaKind::Model && modelView) {
        // 3D models load into the STL element (OpenGL viewer / 2D fallback).
        ShowView(MediaKind::Model);
        surface->ShowImage(nullptr, MediaTransition::NoTransition, 0, false);
        auto* mv = static_cast<UltraCanvasSTLElement*>(modelView.get());
        if (!mv->LoadFromFile(path) && infoLabel)
            infoLabel->SetText("Failed to open 3D model: " + BaseName(path));
        handled = true;
    }
    if (!handled && kind == MediaKind::Text && textView) {
        // Text / source / markdown open read-only in the text area.
        ShowView(MediaKind::Text);
        surface->ShowImage(nullptr, MediaTransition::NoTransition, 0, false);
        auto* ta = static_cast<UltraCanvasTextArea*>(textView.get());
        std::string content;
        if (!ReadTextFile(path, content)) {
            if (infoLabel) infoLabel->SetText("Failed to open text file: " + BaseName(path));
        } else {
            std::string ext = LowerExt(path);
            if (ext == "md" || ext == "markdown") {
                ta->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
                ta->SetHighlightSyntax(false);
            } else {
                ta->SetEditingMode(TextAreaEditingMode::PlainText);
                static SyntaxTokenizer tk;
                if (!ext.empty() && tk.SetLanguageByExtension(ext)) {
                    ta->SetHighlightSyntax(true);
                    ta->SetProgrammingLanguage(tk.GetCurrentProgrammingLanguage());
                } else {
                    ta->SetHighlightSyntax(false);
                }
            }
            ta->SetDisplayOnly(true);   // viewer, not editor (also implies read-only)
            ta->SetText(content);
        }
        handled = true;
    }
    if (!handled && kind == MediaKind::Book && bookView) {
        // e-books open in the reading widget (chapter toolbar + TOC + content).
        ShowView(MediaKind::Book);
        surface->ShowImage(nullptr, MediaTransition::NoTransition, 0, false);
        auto* bv = static_cast<UltraCanvasEBookViewer*>(bookView.get());
        if (!bv->LoadDocument(path) && infoLabel)
            infoLabel->SetText("Failed to open e-book: " + BaseName(path) +
                               " (" + bv->GetLastError() + ")");
        handled = true;
    }
    if (!handled && kind == MediaKind::UCDoc) {
        // UltraCanvas Document container. Full rendering arrives with the UCD
        // v2 engine; until then show the embedded preview thumbnail — stored
        // raw right after the fixed header exactly so viewers can preview
        // without parsing the body — or, without one, a header summary in the
        // text view. The details popup gets the container information either way.
        UCDHeader hdr;
        std::shared_ptr<UCImage> thumb;
        if (ReadUCDHeader(path, hdr)) {
            std::vector<uint8_t> raw;
            if (ReadUCDThumbnail(path, hdr, raw))
                thumb = UCImage::LoadFromMemory(raw);
        }
        bool thumbShown = thumb && thumb->IsValid();
        ucdDetails = BuildUCDDetailsText(path, hdr, thumbShown);
        if (thumbShown) {
            ShowView(MediaKind::Image);
            surface->ShowImage(thumb, transition, transitionDurationMs, animated);
        } else if (textView) {
            ShowView(MediaKind::Text);
            surface->ShowImage(nullptr, MediaTransition::NoTransition, 0, false);
            auto* ta = static_cast<UltraCanvasTextArea*>(textView.get());
            ta->SetEditingMode(TextAreaEditingMode::PlainText);
            ta->SetHighlightSyntax(false);
            ta->SetDisplayOnly(true);
            ta->SetText(ucdDetails);
        }
        handled = true;
    }
#ifdef ULTRACANVAS_ENABLE_VIDEO
    if (!handled && kind == MediaKind::Video && videoPlayer) {
        ShowView(MediaKind::Video);
        surface->ShowImage(nullptr, MediaTransition::NoTransition, 0, false);
        auto* vp = static_cast<UltraCanvasVideoPlayerElement*>(videoPlayer.get());
        // Decide the mute *before* the source is opened, so the engine bakes it
        // into the session it builds. Muting only afterwards leaves a window in
        // which a backend that has not finished wiring its audio renderer drops
        // the request and plays the clip aloud (Media Foundation did exactly
        // that) — the whole point of the muted preview clip.
        if (auto p = vp->GetPlayer())
            p->SetMute(videoPreviewMode == VideoPreviewMode::PreviewClip);
        if (!vp->LoadFromFile(path)) {
            if (infoLabel) infoLabel->SetText("Failed to open video: " + BaseName(path));
        } else {
            ApplyVideoPreviewToCurrent();
        }
        handled = true;
    }
#endif
#ifdef ULTRACANVAS_ENABLE_AUDIO
    if (!handled && kind == MediaKind::Audio && audioPlayer) {
        ShowView(MediaKind::Audio);
        surface->ShowImage(nullptr, MediaTransition::NoTransition, 0, false);
        auto* ap = static_cast<UltraCanvasAudioPlayerElement*>(audioPlayer.get());
        if (!ap->LoadFromFile(path)) {
            if (infoLabel) infoLabel->SetText("Failed to open audio: " + BaseName(path));
        } else {
            ap->Play();
        }
        handled = true;
    }
#endif

    if (!handled) {
        // Image — or a kind whose backend is unavailable, shown best-effort.
        ShowView(MediaKind::Image);
        auto img = UCImage::Get(path);
        surface->ShowImage(img, transition, transitionDurationMs, animated);
    }
    // The strip of backdrop colours belongs to the file just loaded: up for a
    // transparent image, gone for everything else.
    UpdateTransparencyPalette();
    UpdateInfoBar();
    UpdateDetailedInfo();
}

void UltraCanvasMediaViewer::ApplyAdjustments() {
    if (surface) surface->SetAdjustments(adjustments);
}

// ===== PDF DISPLAY SETTINGS =====
// The viewer, not the PDF view, is the host's point of contact: it remembers
// the choice and re-applies it, so a host can set it once and every document
// opened later follows.

void UltraCanvasMediaViewer::ApplyPDFViewSettings() {
#ifdef ULTRACANVAS_PLUGIN_PDF
    if (!pdfView) return;
    auto* pv = static_cast<UltraCanvasPDFView*>(pdfView.get());
    if (pdfThumbAbsolute) pv->SetThumbnailWidth(pdfThumbWidthPx);
    else                  pv->SetThumbnailWidthFraction(pdfThumbWidthFraction);
    pv->SetWheelAction(documentWheelZoom
            ? UltraCanvasPDFView::WheelAction::Zoom
            : UltraCanvasPDFView::WheelAction::Scroll);
#endif
}

void UltraCanvasMediaViewer::SetPDFThumbnailWidth(int pixels) {
    pdfThumbAbsolute = true;
    pdfThumbWidthPx  = std::max(16, pixels);
    ApplyPDFViewSettings();
}

void UltraCanvasMediaViewer::SetPDFThumbnailWidthFraction(float share) {
    pdfThumbAbsolute      = false;
    pdfThumbWidthFraction = std::clamp(share, 0.05f, 0.5f);
    ApplyPDFViewSettings();
}

void UltraCanvasMediaViewer::SetDocumentWheelZoom(bool zoom) {
    documentWheelZoom = zoom;
    ApplyPDFViewSettings();
}

// ===== ZOOM ACTIONS (routed to whichever view is live) =====

void UltraCanvasMediaViewer::ZoomInAction() {
#ifdef ULTRACANVAS_PLUGIN_PDF
    if (activeKind == MediaKind::Document && pdfView) {
        static_cast<UltraCanvasPDFView*>(pdfView.get())->ZoomIn();
        return;
    }
#endif
    if (activeKind == MediaKind::Book && bookView) {
        static_cast<UltraCanvasEBookViewer*>(bookView.get())->ZoomIn();
        return;
    }
    if (activeKind == MediaKind::Image && surface) surface->ZoomBy(1.25);
}

void UltraCanvasMediaViewer::ZoomOutAction() {
#ifdef ULTRACANVAS_PLUGIN_PDF
    if (activeKind == MediaKind::Document && pdfView) {
        static_cast<UltraCanvasPDFView*>(pdfView.get())->ZoomOut();
        return;
    }
#endif
    if (activeKind == MediaKind::Book && bookView) {
        static_cast<UltraCanvasEBookViewer*>(bookView.get())->ZoomOut();
        return;
    }
    if (activeKind == MediaKind::Image && surface) surface->ZoomBy(1.0 / 1.25);
}

void UltraCanvasMediaViewer::ZoomFitAction() {
#ifdef ULTRACANVAS_PLUGIN_PDF
    if (activeKind == MediaKind::Document && pdfView) {
        static_cast<UltraCanvasPDFView*>(pdfView.get())->ZoomToFit();
        return;
    }
#endif
    if (activeKind == MediaKind::Book && bookView) {
        // Books reflow, so "fit" means a comfortable line measure across the pane.
        static_cast<UltraCanvasEBookViewer*>(bookView.get())->ZoomToWidth();
        return;
    }
    if (activeKind == MediaKind::Image && surface) surface->ResetView();
}

void UltraCanvasMediaViewer::ZoomPercentAction(double percent) {
#ifdef ULTRACANVAS_PLUGIN_PDF
    if (activeKind == MediaKind::Document && pdfView) {
        static_cast<UltraCanvasPDFView*>(pdfView.get())->SetZoom(static_cast<float>(percent / 100.0));
        return;
    }
#endif
    if (activeKind == MediaKind::Book && bookView) {
        static_cast<UltraCanvasEBookViewer*>(bookView.get())
                ->SetZoom(static_cast<float>(percent / 100.0));
        return;
    }
    if (activeKind == MediaKind::Image && surface) surface->SetZoomPercent(percent);
}

void UltraCanvasMediaViewer::UpdateInfoBar() {
    if (!infoLabel) return;
    if (playlist.empty()) {
        infoLabel->SetText("No media");
        return;
    }
    const std::string& path = playlist[currentIndex];

    if (!ucdDetails.empty()) {
        // A *.ucd container is showing (as its preview thumbnail or its header
        // summary) — label it as a UCD regardless of which view carries it.
        std::ostringstream os;
        os << BaseName(path) << "   \xC2\xB7   UC DOCUMENT";
        std::error_code ec;
        auto sz = fs::file_size(path, ec);
        if (!ec) os << "   \xC2\xB7   " << HumanSize(sz);
        os << "   \xC2\xB7   " << (currentIndex + 1) << " / " << playlist.size();
        infoLabel->SetText(os.str());
        return;
    }

#ifdef ULTRACANVAS_PLUGIN_PDF
    if (activeKind == MediaKind::Document && pdfView) {
        auto* pv = static_cast<UltraCanvasPDFView*>(pdfView.get());
        std::ostringstream os;
        os << BaseName(path) << "   \xC2\xB7   PDF";
        if (pv->HasDocument()) {
            os << "   \xC2\xB7   page " << pv->GetCurrentPage()
               << " / " << pv->GetPageCount();
        }
        std::error_code ec;
        auto sz = fs::file_size(path, ec);
        if (!ec) os << "   \xC2\xB7   " << HumanSize(sz);
        os << "   \xC2\xB7   " << (currentIndex + 1) << " / " << playlist.size();
        if (pv->HasDocument()) {
            char zbuf[32];
            snprintf(zbuf, sizeof(zbuf), "%.0f%%", pv->GetZoomPercent());
            os << "   \xC2\xB7   " << zbuf;
        }
        infoLabel->SetText(os.str());
        return;
    }
#endif

    if (activeKind == MediaKind::Sheet || activeKind == MediaKind::Model ||
        activeKind == MediaKind::Text || activeKind == MediaKind::Book ||
        activeKind == MediaKind::Video || activeKind == MediaKind::Audio) {
        std::string kindLabel = activeKind == MediaKind::Video ? "VIDEO"
                              : activeKind == MediaKind::Audio ? "AUDIO"
                              : activeKind == MediaKind::Model ? "3D MODEL"
                              : activeKind == MediaKind::Book  ? "EBOOK"
                              : activeKind == MediaKind::Text  ? "TEXT" : "SHEET";
        std::ostringstream os;
        os << BaseName(path) << "   \xC2\xB7   " << kindLabel;
        std::error_code ec;
        auto sz = fs::file_size(path, ec);
        if (!ec) os << "   \xC2\xB7   " << HumanSize(sz);
        os << "   \xC2\xB7   " << (currentIndex + 1) << " / " << playlist.size();
        infoLabel->SetText(os.str());
        return;
    }

    if (!surface || !surface->GetImage() || !surface->GetImage()->IsValid()) {
        infoLabel->SetText("No media");
        return;
    }
    auto img = surface->GetImage();
    std::ostringstream os;
    os << BaseName(path)
       << "   \xC2\xB7   " << img->GetWidth() << " x " << img->GetHeight();

    std::error_code ec;
    auto sz = fs::file_size(path, ec);
    if (!ec) os << "   \xC2\xB7   " << HumanSize(sz);

    std::string ext = LowerExt(path);
    if (!ext.empty()) {
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return (char)std::toupper(c); });
        os << "   \xC2\xB7   " << ext;
    }

    os << "   \xC2\xB7   " << (currentIndex + 1) << " / " << playlist.size();

    char zbuf[32];
    snprintf(zbuf, sizeof(zbuf), "%.0f%%", surface->GetZoomPercent());
    os << "   \xC2\xB7   " << zbuf;

    int rot = surface->GetRotationQuarters();
    if (rot != 0) os << "   \xC2\xB7   " << (rot * 90) << "\xC2\xB0";

    infoLabel->SetText(os.str());
}

void UltraCanvasMediaViewer::UpdateDetailedInfo() {
    if (!surface || playlist.empty()) return;
    const std::string& path = playlist[currentIndex];

    if (!ucdDetails.empty()) {
        // The UCD container summary was built while loading the file.
        surface->SetInfoText(ucdDetails);
        return;
    }

    if (activeKind == MediaKind::Book && bookView) {
        auto* bv = static_cast<UltraCanvasEBookViewer*>(bookView.get());
        std::ostringstream bos;
        bos << "eBook information\n\n";
        bos << "File: " << BaseName(path) << "\n";
        bos << "Path: " << path << "\n";
        std::error_code bec;
        auto bsz = fs::file_size(path, bec);
        if (!bec) bos << "Size: " << HumanSize(bsz) << "\n";
        std::string ext = LowerExt(path);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return (char)std::toupper(c); });
        bos << "Type: eBook (" << ext << ")\n";
        if (const EBookMetadata* md = bv->GetMetadata()) {
            if (!md->title.empty()) bos << "Title: " << md->title << "\n";
            if (!md->authors.empty()) {
                bos << "Author: ";
                for (size_t i = 0; i < md->authors.size(); ++i) {
                    if (i) bos << ", ";
                    bos << md->authors[i];
                }
                bos << "\n";
            }
            if (!md->publisher.empty()) bos << "Publisher: " << md->publisher << "\n";
            if (!md->language.empty())  bos << "Language: " << md->language << "\n";
        }
        if (bv->IsDocumentLoaded())
            bos << "Chapters: " << bv->GetChapterCount() << "\n";
        surface->SetInfoText(bos.str());
        return;
    }

#ifdef ULTRACANVAS_PLUGIN_PDF
    if (activeKind == MediaKind::Document && pdfView) {
        // Document metadata comes from the PDF element, never from libvips.
        auto* pv = static_cast<UltraCanvasPDFView*>(pdfView.get());
        std::ostringstream dos;
        dos << "Document information\n\n";
        dos << "File: " << BaseName(path) << "\n";
        dos << "Path: " << path << "\n";
        std::error_code dec;
        auto dsz = fs::file_size(path, dec);
        if (!dec) dos << "Size: " << HumanSize(dsz) << "\n";
        dos << "Type: PDF document\n";
        if (pv->HasDocument()) dos << "Pages: " << pv->GetPageCount() << "\n";
        surface->SetInfoText(dos.str());
        return;
    }
#endif

    if (activeKind == MediaKind::Sheet || activeKind == MediaKind::Model ||
        activeKind == MediaKind::Text || activeKind == MediaKind::Video ||
        activeKind == MediaKind::Audio) {
        const char* heading = activeKind == MediaKind::Video ? "Video information\n\n"
                            : activeKind == MediaKind::Audio ? "Audio information\n\n"
                            : activeKind == MediaKind::Model ? "3D model information\n\n"
                            : activeKind == MediaKind::Text  ? "Text information\n\n"
                                                             : "Spreadsheet information\n\n";
        const char* typeName = activeKind == MediaKind::Video ? "Video"
                             : activeKind == MediaKind::Audio ? "Audio"
                             : activeKind == MediaKind::Model ? "3D model"
                             : activeKind == MediaKind::Text  ? "Text" : "Spreadsheet";
        std::ostringstream mos;
        mos << heading;
        mos << "File: " << BaseName(path) << "\n";
        mos << "Path: " << path << "\n";
        std::error_code mec;
        auto msz = fs::file_size(path, mec);
        if (!mec) mos << "Size: " << HumanSize(msz) << "\n";
        std::string ext = LowerExt(path);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return (char)std::toupper(c); });
        mos << "Type: " << typeName << " (" << ext << ")\n";
        surface->SetInfoText(mos.str());
        return;
    }

    std::ostringstream os;
    os << "Image information\n\n";
    os << "File: " << BaseName(path) << "\n";
    os << "Path: " << path << "\n";

    std::error_code ec;
    auto sz = fs::file_size(path, ec);
    if (!ec) os << "Size: " << HumanSize(sz) << "\n";

    if (auto img = surface->GetImage()) {
        os << "Dimensions: " << img->GetWidth() << " x " << img->GetHeight() << " px\n";
        if (img->IsAnimated()) {
            os << "Animation: " << img->GetFrameCount() << " frames\n";
        }
    }

#ifdef HAS_LIBVIPS
    try {
        PixelFX::PXImageFileInfo fi = PixelFX::ExtractImageInfo(path);
        os << "Channels: " << fi.channels << "\n";
        os << "Bits/channel: " << fi.bitsPerChannel << "\n";
        if (!fi.colorSpace.empty()) os << "Colour space: " << fi.colorSpace << "\n";
        os << "Alpha: " << (fi.hasAlpha ? "yes" : "no") << "\n";
        if (fi.dpiX > 0 || fi.dpiY > 0)
            os << "Resolution: " << fi.dpiX << " x " << fi.dpiY << " dpi\n";
        if (!fi.loader.empty()) os << "Loader: " << fi.loader << "\n";
    } catch (...) {
        // Metadata extraction is best-effort.
    }
#endif

    surface->SetInfoText(os.str());
}

// ===== SLIDESHOW =====

void UltraCanvasMediaViewer::PlaySlideshow() {
    slideshowPlaying = true;
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app) return;
    if (slideshowTimer) app->StopTimer(slideshowTimer);
    unsigned ms = static_cast<unsigned>(std::max(0.5, slideshowIntervalSec) * 1000.0);
    slideshowTimer = app->StartTimer(ms, true, [this](TimerId) { Next(); });
}

void UltraCanvasMediaViewer::PauseSlideshow() {
    slideshowPlaying = false;
    if (slideshowTimer) {
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(slideshowTimer);
        slideshowTimer = 0;
    }
}

void UltraCanvasMediaViewer::ToggleSlideshow() {
    if (slideshowPlaying) PauseSlideshow();
    else                  PlaySlideshow();
}

void UltraCanvasMediaViewer::SetSlideshowIntervalSeconds(double sec) {
    slideshowIntervalSec = std::max(0.5, sec);
    if (slideshowPlaying) { PauseSlideshow(); slideshowPlaying = true; PlaySlideshow(); }
}

// ===== VIDEO PREVIEW =====

void UltraCanvasMediaViewer::StopVideoClipTimer() {
    // Whoever stops the clip decides the mute state itself (a new file, a mode
    // change, StopPlayback), so a finished clip's deferred un-mute is dropped
    // here rather than firing into the next source.
    videoClipUnmutePending = false;
    if (!videoClipTimer) return;
    if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(videoClipTimer);
    videoClipTimer = 0;
}

void UltraCanvasMediaViewer::ApplyVideoPreviewToCurrent() {
    StopVideoClipTimer();
#ifdef ULTRACANVAS_ENABLE_VIDEO
    if (!videoPlayer) return;
    auto* vp = static_cast<UltraCanvasVideoPlayerElement*>(videoPlayer.get());
    auto player = vp->GetPlayer();
    if (!player || !player->IsLoaded()) return;
    switch (videoPreviewMode) {
        case VideoPreviewMode::Autoplay:
            player->SetMute(false);
            vp->Play();
            break;
        case VideoPreviewMode::PreviewClip: {
            // Album hover-preview style: a few seconds of muted playback,
            // then pause on the current frame (the transport bar stays live
            // for the user to continue watching).
            player->SetMute(true);
            vp->Seek(0.0);
            vp->Play();
            auto* app = UltraCanvasApplication::GetInstance();
            if (app) {
                unsigned ms = static_cast<unsigned>(
                        std::max(0.5f, videoPreviewClipSec) * 1000.0f);
                videoClipTimer = app->StartTimer(ms, false, [this](TimerId) {
                    videoClipTimer = 0;
#ifdef ULTRACANVAS_ENABLE_VIDEO
                    if (!videoPlayer) return;
                    auto* v = static_cast<UltraCanvasVideoPlayerElement*>(videoPlayer.get());
                    v->Pause();
                    // Stay muted, and un-mute on the user's own resume instead
                    // (see the onPlay hook). Un-muting here would let the sound
                    // out whenever the pause is deferred — a backend that is
                    // still settling a flushing seek applies the pause after
                    // the fact, and the clip would keep playing, now audibly.
                    videoClipUnmutePending = true;
#endif
                });
            }
            break;
        }
        case VideoPreviewMode::Still:
            // Stay paused and show the first frame. The load-time preroll is
            // not a reliable frame source on its own (its one emission can be
            // lost while the pipeline is still settling), so when no frame of
            // this source has been shown yet, request one explicitly: a paused
            // seek to 0 makes the sink re-preroll and deliver the frame — the
            // same mechanism that refreshes the surface on a paused scrub.
            player->SetMute(false);
            if (!vp->HasVideoFrame()) vp->Seek(0.0);
            break;
    }
#endif
}

void UltraCanvasMediaViewer::SetVideoPreviewMode(VideoPreviewMode mode) {
    if (mode == videoPreviewMode) return;
    videoPreviewMode = mode;
    // Apply to a currently shown video so the change is immediately visible.
#ifdef ULTRACANVAS_ENABLE_VIDEO
    if (videoPlayer && videoPlayer->IsVisible()) {
        if (mode == VideoPreviewMode::Still) {
            StopVideoClipTimer();
            auto* vp = static_cast<UltraCanvasVideoPlayerElement*>(videoPlayer.get());
            vp->Pause();
            if (auto p = vp->GetPlayer()) p->SetMute(false);
            // No frame shown yet (e.g. the mode changed right after a load):
            // a paused seek re-prerolls and delivers the still frame.
            if (!vp->HasVideoFrame()) vp->Seek(0.0);
        } else {
            ApplyVideoPreviewToCurrent();
        }
    }
#endif
}

void UltraCanvasMediaViewer::SetVideoPreviewClipSeconds(float seconds) {
    videoPreviewClipSec = std::max(0.5f, seconds);
}

void UltraCanvasMediaViewer::StopPlayback() {
    StopVideoClipTimer();
#ifdef ULTRACANVAS_ENABLE_VIDEO
    if (videoPlayer) static_cast<UltraCanvasVideoPlayerElement*>(videoPlayer.get())->Stop();
#endif
#ifdef ULTRACANVAS_ENABLE_AUDIO
    if (audioPlayer) static_cast<UltraCanvasAudioPlayerElement*>(audioPlayer.get())->Stop();
#endif
}

// ===== EVENTS =====

void UltraCanvasMediaViewer::HandleDroppedFiles(const std::vector<std::string>& files) {
    if (files.empty()) return;
    std::error_code ec;
    // A single dropped folder browses that folder; a single dropped file browses
    // the folder it lives in (so the arrow keys have somewhere to go) with that
    // file shown first. Several files are an explicit list (folders expanded).
    if (files.size() == 1 && fs::is_directory(files[0], ec)) {
        OpenFolder(files[0]);
        return;
    }
    if (files.size() == 1 && IsSupportedMedia(files[0])) {
        OpenFile(files[0]);
        return;
    }
    SetFiles(files, 0);
}

// ===========================================================================
// KEYBOARD
// ===========================================================================
// Two paths lead here. When the widget (or one of its toolbar controls) holds
// the focus the key event bubbles into OnEvent(). When the focus is nowhere —
// the common case right after the widget appears — or sits on a display view
// that ignores or swallows the browsing keys, the window key filter catches it
// before normal dispatch. Both end in HandleViewerKey().

std::string UltraCanvasMediaViewer::KeyFilterId() const {
    return GetIdentifier() + "_MediaViewerKeys";
}

bool UltraCanvasMediaViewer::IsEffectivelyVisible() const {
    if (!IsVisible()) return false;
    for (const UltraCanvasUIElement* e = GetParentContainer(); e; e = e->GetParentContainer()) {
        if (!e->IsVisible()) return false;
    }
    return true;
}

UltraCanvasUIElement* UltraCanvasMediaViewer::ActiveViewElement() const {
    switch (activeKind) {
        case MediaKind::Document: return pdfView.get();
        case MediaKind::Sheet:    return sheetView.get();
        case MediaKind::Model:    return modelView.get();
        case MediaKind::Text:     return textView.get();
        case MediaKind::Book:     return bookView.get();
        case MediaKind::Video:    return videoPlayer.get();
        case MediaKind::Audio:    return audioPlayer.get();
        case MediaKind::Image:
        default:                  return surface.get();
    }
}

bool UltraCanvasMediaViewer::IsDisplayView(const UltraCanvasUIElement* element) const {
    if (!element) return false;
    return element == surface.get()     || element == pdfView.get() ||
           element == sheetView.get()   || element == modelView.get() ||
           element == textView.get()    || element == bookView.get() ||
           element == videoPlayer.get() || element == audioPlayer.get();
}

bool UltraCanvasMediaViewer::FocusForKeyboard() {
    return SetFocus(true);
}

void UltraCanvasMediaViewer::InstallKeyFilter() {
    auto* win = GetWindow();
    if (!win || keyFilterInstalled) return;
    keyFilterInstalled = true;
    win->InstallEventFilter(KeyFilterId(),
            [this](const UCEvent& e) -> bool { return HandleFilteredKey(e); },
            { UCEventType::KeyDown });
}

void UltraCanvasMediaViewer::RemoveKeyFilter() {
    if (!keyFilterInstalled) return;
    keyFilterInstalled = false;
    if (auto* win = GetWindow()) win->UnInstallWindowEventFilter(KeyFilterId());
}

void UltraCanvasMediaViewer::SetWindow(UltraCanvasWindowBase* win) {
    if (GetWindow() && GetWindow() != win) RemoveKeyFilter();
    UltraCanvasContainer::SetWindow(win);
    if (!win) return;
    InstallKeyFilter();
    // Claim the keyboard so the arrow keys browse straight away instead of only
    // after a click into the picture.
    if (grabFocusOnAttach) FocusForKeyboard();
}

bool UltraCanvasMediaViewer::HandleFilteredKey(const UCEvent& event) {
    if (event.type != UCEventType::KeyDown) return false;
    if (!IsEffectivelyVisible()) return false;
    auto* win = GetWindow();
    if (!win) return false;
    // An open dropdown / menu owns the keyboard while it is up.
    if (win->GetActivePopupElement()) return false;

    UltraCanvasUIElement* focused = win->GetFocusedElement();
    // Step in only when the keyboard is unowned or held by one of the display
    // views. Toolbar buttons, sliders and the breadcrumb keep their own key
    // handling — their events bubble into OnEvent() instead.
    if (focused && focused != this && !IsDisplayView(focused)) return false;

    return HandleViewerKey(event);
}

bool UltraCanvasMediaViewer::HandleViewerKey(const UCEvent& event) {
    if (event.type != UCEventType::KeyDown) return false;

    // File browsing. Alt+Left / Alt+Right browse even while the active view
    // claims the bare arrows for itself (spreadsheet cell movement).
    const bool viewOwnsArrows = ActiveViewUsesArrowKeys();
    switch (event.virtualKey) {
        case UCKeys::Left:
            if (viewOwnsArrows && !event.alt) return false;
            Previous();
            return true;
        case UCKeys::Right:
            if (viewOwnsArrows && !event.alt) return false;
            Next();
            return true;
        case UCKeys::Space:
            if (viewOwnsArrows) return false;
            ToggleSlideshow();
            return true;
        default:
            break;
    }

    // The text view is display-only, so scrolling it is the widget's job.
    if (activeKind == MediaKind::Text && textView) {
        auto* ta = static_cast<UltraCanvasTextArea*>(textView.get());
        switch (event.virtualKey) {
            case UCKeys::Up:       ta->ScrollUp(1);     return true;
            case UCKeys::Down:     ta->ScrollDown(1);   return true;
            case UCKeys::PageUp:   ta->ScrollUp(10);    return true;
            case UCKeys::PageDown: ta->ScrollDown(10);  return true;
            default: break;
        }
        if (event.ctrl && (event.character == 'c' || event.character == 'C')) {
            ta->CopySelection();
            return true;
        }
        return false;
    }

    // Everything else belongs to the active view: image zoom / rotate on the
    // surface, page keys in the PDF view, cell movement in the spreadsheet. The
    // view handles those itself while it holds the focus; while the widget holds
    // it (the usual case now) they are forwarded.
    if (UltraCanvasUIElement* view = ActiveViewElement()) {
        if (!view->IsFocused()) return view->OnEvent(event);
    }
    return false;
}

bool UltraCanvasMediaViewer::OnEvent(const UCEvent& event) {
    if (event.type == UCEventType::Drop) {
        HandleDroppedFiles(event.droppedFiles);
        return true;
    }
    if (UltraCanvasContainer::OnEvent(event)) return true;

    return HandleViewerKey(event);
}

} // namespace UltraCanvas
