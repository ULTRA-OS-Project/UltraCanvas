// include/UltraCanvasMediaViewer.h
// Comprehensive media / photo / document viewer widget for UltraCanvas.
// Designed to be embedded (e.g. inside the future UltraFiler) as a single
// reusable widget. Provides folder/selection browsing, next/previous
// navigation (arrow keys or left/right mouse click — even while zoomed),
// a timed slideshow with selectable transitions, manual + automatic zoom,
// rotation, horizontal/vertical mirroring, gamma / brightness / per-channel
// colour correction, auto-optimisation, sharpening, save-as in many formats,
// a bottom information bar and a detailed info popup. Images are loaded and
// saved through the framework's file loader (UCImage / UltraCanvasFileLoader)
// and pixel manipulation is performed with PixelFX (libvips).
//
// Drag a folder onto the widget to browse it; drag one or more files to view
// them.
//
// Version: 1.0.0
// Last Modified: 2026-06-26
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasTimer.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>

namespace UltraCanvas {

// Forward declarations of widgets we compose with.
class UltraCanvasToolbar;
class UltraCanvasButton;
class UltraCanvasDropdown;
class UltraCanvasLabel;
class UltraCanvasSlider;
class UltraCanvasPDFView;   // PDF documents render through this element (MuPDF),
                           // not through the raster/libvips path.

// ===== TRANSITION STYLES BETWEEN IMAGES =====
// (Suffixed names avoid clashing with X11 macros such as None.)
enum class MediaTransition {
    NoTransition,      // Instant swap
    CrossFade,         // Both layers visible, sums to 1.0
    FadeOutIn,         // Old fades fully out, then new fades in
    SlideHorizontal,   // Old slides left while new slides in from the right
    SlideVertical,     // Old slides up while new slides in from the bottom
    ZoomFade           // New image zooms in from slightly enlarged while cross-fading
};

// ===== TONE / COLOUR ADJUSTMENTS =====
// Applied through PixelFX to produce the displayed (and saved) pixels.
// Identity values leave the source untouched (and skip processing entirely).
struct MediaAdjustments {
    double gamma      = 1.0;   // 1.0 = no change (PixelFX::Colour::Gamma)
    double brightness = 1.0;   // multiplicative, 1.0 = no change
    double red        = 1.0;   // per-channel multipliers, 1.0 = no change
    double green      = 1.0;
    double blue       = 1.0;
    double sharpen    = 0.0;   // libvips sharpen sigma, 0 = off
    bool   autoOptimize = false; // histogram equalisation

    bool IsIdentity() const {
        return gamma == 1.0 && brightness == 1.0 &&
               red == 1.0 && green == 1.0 && blue == 1.0 &&
               sharpen <= 0.0 && !autoOptimize;
    }
};

// ===== CENTRAL DISPLAY SURFACE =====
// Owns the currently displayed image plus its view geometry (zoom about the
// fit scale, pan, rotation in 90° steps, horizontal/vertical mirror) and the
// colour-processed pixmap. Re-rasterises the image at the displayed size each
// frame so it stays crisp at any zoom. Geometry (rotation / mirror / zoom /
// pan) is applied with the render-context transform, so it needs no
// re-processing; only the tone/colour adjustments go through PixelFX.
class UltraCanvasMediaSurface : public UltraCanvasUIElement {
public:
    explicit UltraCanvasMediaSurface(const std::string& elemId = "MediaSurface");
    ~UltraCanvasMediaSurface() override;

    // Show a new image, optionally animating the swap with `transition`.
    void ShowImage(std::shared_ptr<UCImage> img, MediaTransition transition,
                   int durationMs, bool animated);
    std::shared_ptr<UCImage> GetImage() const { return image; }

    // Tone / colour adjustments (rebuilds the processed pixmap).
    void SetAdjustments(const MediaAdjustments& adj);
    const MediaAdjustments& GetAdjustments() const { return adjust; }

    // ===== VIEW GEOMETRY =====
    void ResetView();                       // back to fit-to-window, centred
    void SetZoomPercent(double percent);    // 100 == native pixels
    void ZoomBy(double factor);             // multiply the current zoom
    double GetZoomPercent() const;
    bool IsAutoFit() const;                 // true when zoom == fit
    void RotateBy(int quarters);            // +1 == 90° clockwise
    void SetRotationQuarters(int q);
    int  GetRotationQuarters() const { return rotationQuarters; }
    void ToggleFlipHorizontal();
    void ToggleFlipVertical();
    void ResetTransforms();                 // rotation + mirror only

    // Bake the current adjustments + geometry into a file (PixelFX). Returns
    // false and fills `error` on failure.
    bool SaveProcessed(const std::string& path, std::string& error);

    // ===== INFO POPUP OVERLAY =====
    void SetInfoText(const std::string& text) { infoText = text; RequestRedraw(); }
    void ToggleInfoPopup() { showInfoPopup = !showInfoPopup; RequestRedraw(); }
    bool IsInfoPopupVisible() const { return showInfoPopup; }

    void SetCanvasColor(const Color& c) { canvasColor = c; RequestRedraw(); }

    // Called when the user navigates by click / arrow (delta -1 == previous,
    // +1 == next) and when the view geometry changes (to refresh the info bar).
    std::function<void(int)> onNavigate;
    std::function<void()>    onViewChanged;
    // Called when files are dropped onto the surface (so the host viewer can
    // load them). Lets the drop work whether the event lands on the surface or
    // bubbles up to the viewer.
    std::function<void(const std::vector<std::string>&)> onFilesDropped;

    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;
    bool AcceptsFocus() const override { return true; }

private:
    double FitScale(double iw, double ih, int rotQ) const;  // uses current bounds
    void   RebuildProcessed();
    bool   HandleWheelZoom(const UCEvent& event);

    // Draw `img` (or its colour-processed `pm`) into local bounds `b` with the
    // given orientation, scale, centre offset and opacity.
    void Blit(IRenderContext* ctx, const std::shared_ptr<UCImage>& img,
              const std::shared_ptr<UCPixmap>& pm, double iw, double ih,
              double scale, double cx, double cy, int rotQ, bool fH, bool fV,
              double alpha);

    void DrawCurrent(IRenderContext* ctx, const Rect2Df& b);
    void DrawInfoOverlay(IRenderContext* ctx, const Rect2Df& b);
    void StartTransitionTimer(int durationMs);
    void StopTransitionTimer();

    std::shared_ptr<UCImage>  image;
    std::shared_ptr<UCPixmap> processed;     // colour-adjusted full-res pixmap (or null)
    MediaAdjustments adjust;

    Color canvasColor = Color(24, 24, 28, 255);

    // View geometry for the current image.
    double zoom = 1.0;        // multiple of the fit scale (1.0 == fit)
    double panX = 0.0, panY = 0.0;
    int    rotationQuarters = 0;
    bool   flipH = false, flipV = false;

    // Pointer interaction (click vs drag arbitration).
    bool pressing = false, dragging = false;
    double pressX = 0, pressY = 0, lastX = 0, lastY = 0;
    UCMouseButton pressButton = UCMouseButton::NoneButton;

    // Transition animation.
    bool transitionActive = false;
    MediaTransition transitionStyle = MediaTransition::NoTransition;
    int  transitionDurationMs = 400;
    double transitionProgress = 0.0;
    std::chrono::steady_clock::time_point transitionStart;
    TimerId transitionTimer = 0;
    std::shared_ptr<UCImage>  prevImage;
    std::shared_ptr<UCPixmap> prevProcessed;
    int  prevRotQ = 0;
    bool prevFlipH = false, prevFlipV = false;

    // Info popup overlay.
    bool showInfoPopup = false;
    std::string infoText;
};

// ===== THE MEDIA VIEWER WIDGET =====
// A self-contained column of [toolbar][adjustments panel][image surface]
// [info bar]. Manages the playlist of files, slideshow timing and the
// transition selection; the surface handles the actual display and editing.
class UltraCanvasMediaViewer : public UltraCanvasContainer {
public:
    UltraCanvasMediaViewer(const std::string& identifier,
                           float x, float y, float w, float h);
    ~UltraCanvasMediaViewer() override;

    // ===== CONTENT =====
    // Browse every supported media file in a folder. If `selectFile` names a
    // file inside the folder it becomes the first shown.
    void OpenFolder(const std::string& folderPath, const std::string& selectFile = "");
    // Show an explicit list of files (directories in the list are expanded).
    void SetFiles(const std::vector<std::string>& files, size_t startIndex = 0);
    // Open a single file and browse the rest of its folder.
    void OpenFile(const std::string& filePath);
    // Native open dialog (multi-select) through UltraCanvasFileLoader.
    void ShowOpenDialog();

    size_t GetCount() const { return playlist.size(); }
    size_t GetCurrentIndex() const { return currentIndex; }
    std::string GetCurrentPath() const;

    // ===== NAVIGATION =====
    void Next();
    void Previous();
    void GoTo(size_t index, bool animated);

    // ===== SLIDESHOW =====
    void PlaySlideshow();
    void PauseSlideshow();
    void ToggleSlideshow();
    bool IsSlideshowPlaying() const { return slideshowPlaying; }
    void SetSlideshowIntervalSeconds(double sec);
    double GetSlideshowIntervalSeconds() const { return slideshowIntervalSec; }

    void SetTransition(MediaTransition t) { transition = t; }
    MediaTransition GetTransition() const { return transition; }

    UltraCanvasMediaSurface* GetSurface() const { return surface.get(); }

    bool OnEvent(const UCEvent& event) override;

    // Whether a path is a media file this viewer can display.
    static bool IsSupportedMedia(const std::string& path);

private:
    void BuildUI(float w, float h);
    void LoadCurrent(bool animated);
    void UpdateInfoBar();
    void UpdateDetailedInfo();
    void ApplyAdjustments();          // push `adjustments` to the surface
    void ShowSaveDialog();
    void HandleDroppedFiles(const std::vector<std::string>& files);
    // Zoom toolbar actions route to the PDF view or the image surface depending
    // on which is currently showing.
    void ZoomInAction();
    void ZoomOutAction();
    void ZoomFitAction();
    void ZoomPercentAction(double percent);
    static bool IsDocumentFile(const std::string& path);   // PDF (and other docs)
    std::shared_ptr<UltraCanvasUIElement> BuildAdjustSlider(
            const std::string& id, const std::string& caption,
            float minV, float maxV, float value, std::function<void(float)> onChange);
    static std::vector<std::string> EnumerateFolder(const std::string& folder);

    std::vector<std::string> playlist;
    size_t currentIndex = 0;

    std::shared_ptr<UltraCanvasToolbar>      toolbar;   // navigation / slideshow row
    std::shared_ptr<UltraCanvasToolbar>      toolbar2;  // view / edit row
    std::shared_ptr<UltraCanvasContainer>    adjustPanel;
    std::shared_ptr<UltraCanvasMediaSurface> surface;
    std::shared_ptr<UltraCanvasContainer>    bottomBar;
    std::shared_ptr<UltraCanvasLabel>        infoLabel;
    std::shared_ptr<UltraCanvasButton>       playButton;
    // PDF view element (UltraCanvasPDFView), shown instead of the image surface
    // when the current file is a document. Held as the base type so the public
    // header need not include the PDF plugin; only constructed when the PDF
    // plugin is compiled in. pdfActive marks which view is live.
    std::shared_ptr<UltraCanvasUIElement>    pdfView;
    bool pdfActive = false;

    MediaAdjustments adjustments;

    bool   slideshowPlaying = false;
    double slideshowIntervalSec = 5.0;
    TimerId slideshowTimer = 0;
    MediaTransition transition = MediaTransition::CrossFade;
    int transitionDurationMs = 450;
};

// ===== FACTORY =====
inline std::shared_ptr<UltraCanvasMediaViewer> CreateMediaViewer(
        const std::string& identifier, float x, float y, float w, float h) {
    return std::make_shared<UltraCanvasMediaViewer>(identifier, x, y, w, h);
}

} // namespace UltraCanvas
