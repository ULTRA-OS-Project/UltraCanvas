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
// Beyond images it also opens documents, spreadsheets, 3D models, e-books,
// audio and video: PDFs render through UltraCanvasPDFView (MuPDF),
// spreadsheets (ODS / CSV / TSV) through UltraCanvasSpreadsheet, STL 3D models
// through the OpenGL model viewer (UltraCanvasSTLElement), text / source /
// markdown files through a read-only UltraCanvasTextArea (syntax highlighting
// + markdown rendering), e-books (EPUB / FB2 / MOBI / AZW) through
// UltraCanvasEBookViewer (engine registry), and audio / video through the
// framework's UltraCanvasAudioPlayerElement / UltraCanvasVideoPlayerElement.
// UltraCanvas Document containers (*.ucd) are recognised too: until the UCD v2
// engine lands, the viewer shows the container's embedded preview thumbnail
// (readable without parsing the body — the format is designed for that) plus
// the header details; files without a thumbnail get a header summary. The
// right view is chosen automatically from the file kind; image-only tools
// (zoom, rotate, adjustments, save) apply to images, and zoom also drives the
// PDF and e-book views.
// (ODT is an OpenDocument *text* document, not a spreadsheet, so it is not
// handled by the spreadsheet engine.)
//
// A folder breadcrumb (Parallelogram style) sits at the top, built with the
// same path mechanism the filer uses (BuildFolderBreadcrumb): a leading
// "Computer" node whose dropdown lists every drive / mounted volume, the drive
// (or root) node, then one node per folder — the path separator itself is never
// a node. Each segment opens that folder, and its dropdown lists the folders
// inside that segment so the path can be extended one level; long paths
// collapse their middle into a "..." overflow menu.
//
// Drag a folder onto the widget to browse it; drag a single file to browse the
// folder that file lives in (it is shown first), or several files to view just
// those. The Open dialog follows the same rule.
//
// Keyboard: the widget claims the window keyboard focus when it is attached to
// a window (SetGrabFocusOnAttach(false) opts out), so Left / Right browse the
// folder without clicking the picture first. It also filters the window's key
// events, which keeps browsing alive while one of the display views holds the
// focus — text files are shown display-only for exactly that reason. Where a
// view owns the bare arrows itself (spreadsheet cell movement) Alt+Left /
// Alt+Right still browse.
//
// Transparent images draw over a configurable backdrop: either a preset
// solid colour (default white) or the checkered pattern familiar from image
// editors (SetTransparentBackground / SetTransparentColor).
//
// Version: 1.4.0
// Last Modified: 2026-08-06
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasImageAnimation.h"
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
class UltraCanvasBreadcrumb;   // folder path strip at the top (Parallelogram style)
class UltraCanvasPDFView;             // PDF documents (MuPDF), not the raster path
class UltraCanvasVideoPlayerElement; // video playback (platform media backend)
class UltraCanvasAudioPlayerElement; // audio playback (audio backend)
class UltraCanvasSpreadsheet;        // ODS / CSV / TSV spreadsheets
class UltraCanvasSTLElement;         // STL 3D models (OpenGL viewer, 2D fallback)
class UltraCanvasTextArea;           // text / source / markdown (read-only)
class UltraCanvasEBookViewer;        // EPUB / FB2 / MOBI e-books (engine registry)

// ===== WHAT KIND OF MEDIA A FILE IS =====
// Chooses which child element renders it: images go through the image surface,
// documents through the PDF view, spreadsheets (ODS/CSV/TSV) through the
// spreadsheet element, 3D models (STL) through the OpenGL model viewer, text /
// source / markdown files through a read-only text area, e-books through the
// eBook viewer, and audio/video through their player elements. UCDoc marks an
// UltraCanvas Document container (*.ucd); it is displayed through the image
// surface (embedded preview thumbnail) or the text area (header summary), so
// it never becomes the active view kind itself.
enum class MediaKind { Image, Document, Sheet, Model, Text, Book, UCDoc, Video, Audio };

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

// ===== BACKDROP BEHIND TRANSPARENT IMAGES =====
// What is drawn directly behind the image, visible wherever the image has
// transparent pixels. Independent of the surrounding canvas colour.
enum class TransparentImageBackground {
    SolidColor,   // flat preset colour (default white)
    Checkered     // light/dark checkerboard, as used by image editors
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

    // ===== BACKDROP BEHIND TRANSPARENT IMAGES =====
    // Drawn under the image (matching its displayed rectangle) so transparent
    // pixels read against a defined background instead of the canvas colour.
    void SetTransparentBackground(TransparentImageBackground mode) {
        transparentBackground = mode; RequestRedraw();
    }
    TransparentImageBackground GetTransparentBackground() const { return transparentBackground; }
    // The preset colour used by TransparentImageBackground::SolidColor.
    void SetTransparentColor(const Color& c) { transparentColor = c; RequestRedraw(); }
    const Color& GetTransparentColor() const { return transparentColor; }

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

    // Draw the transparency backdrop (solid colour or checkerboard) under the
    // image's displayed rectangle, clipped to the widget bounds `b`. Rotation
    // is in 90° steps, so the rectangle stays axis-aligned; `alpha` matches
    // the image layer's opacity during transitions.
    void DrawBackdrop(IRenderContext* ctx, const Rect2Df& b, double iw, double ih,
                      double scale, double cx, double cy, int rotQ, double alpha);

    void DrawCurrent(IRenderContext* ctx, const Rect2Df& b);
    void DrawInfoOverlay(IRenderContext* ctx, const Rect2Df& b);
    void StartTransitionTimer(int durationMs);
    void StopTransitionTimer();

    std::shared_ptr<UCImage>  image;
    std::shared_ptr<UCPixmap> processed;     // colour-adjusted full-res pixmap (or null)
    MediaAdjustments adjust;

    // Frame stepping for animated images (GIF, animated WebP). Plays while
    // the tone/colour adjustments are identity; a non-identity adjustment
    // freezes playback on the current frame (adjustments are baked into a
    // single composited pixmap).
    UCImageAnimationController animator;

    Color canvasColor = Color(24, 24, 28, 255);

    // Backdrop under transparent images (see SetTransparentBackground).
    TransparentImageBackground transparentBackground = TransparentImageBackground::SolidColor;
    Color transparentColor = Color(255, 255, 255, 255);

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

// How the viewer behaves when a video file becomes the shown item.
enum class VideoPreviewMode {
    Autoplay,      // start full playback with sound (default)
    PreviewClip,   // play the first few seconds muted, then pause
                   // (the UltraCanvasAlbum hover-preview style)
    Still          // show the prerolled first frame, paused
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
    // Native open dialog (multi-select) through UltraCanvasFileLoader. Picking a
    // single file browses its folder (OpenFile); picking several sets them as
    // the playlist (SetFiles).
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

    // ===== VIDEO PREVIEW =====
    // Selects what happens when a video file is shown (see VideoPreviewMode).
    // Changing the mode also applies it to a currently shown video: Autoplay
    // resumes playback with sound, PreviewClip restarts the muted clip and
    // Still pauses.
    void SetVideoPreviewMode(VideoPreviewMode mode);
    VideoPreviewMode GetVideoPreviewMode() const { return videoPreviewMode; }
    // Length of the muted PreviewClip playback (default 5 s, like the
    // UltraCanvasAlbum hover preview).
    void SetVideoPreviewClipSeconds(float seconds);
    float GetVideoPreviewClipSeconds() const { return videoPreviewClipSec; }
    // Stop any running video / audio playback (and a pending PreviewClip
    // timer). For hosts that hide or detach the viewer: without this the
    // sound would keep playing while nothing is visible.
    void StopPlayback();

    UltraCanvasMediaSurface* GetSurface() const { return surface.get(); }

    // ===== BACKDROP BEHIND TRANSPARENT IMAGES =====
    // Forwarded to the display surface: what transparent image pixels read
    // against — a preset solid colour (default white) or the checkered
    // pattern used by image editors.
    void SetTransparentBackground(TransparentImageBackground mode);
    TransparentImageBackground GetTransparentBackground() const;
    void SetTransparentColor(const Color& c);
    Color GetTransparentColor() const;

    // ===== TOP BARS (EMBEDDED MODE) =====
    // Show/hide everything above the display surface: the folder breadcrumb,
    // both toolbar rows and the adjustments panel. Hosts that embed the viewer
    // as a plain preview pane (e.g. the UltraFiler preview) turn them off and
    // provide their own navigation. Default: visible.
    void SetTopBarsVisible(bool visible);
    bool GetTopBarsVisible() const { return topBarsVisible; }

    // ===== KEYBOARD =====
    // Give the widget the window keyboard focus, so the browsing keys work
    // without clicking into it first.
    bool FocusForKeyboard();
    // Whether the widget takes the keyboard focus when it is added to a window
    // (default true). Turn it off when the host wants to place the initial
    // focus itself.
    void SetGrabFocusOnAttach(bool grab) { grabFocusOnAttach = grab; }
    bool GetGrabFocusOnAttach() const { return grabFocusOnAttach; }

    bool AcceptsFocus() const override { return true; }
    void SetWindow(UltraCanvasWindowBase* win) override;
    bool OnEvent(const UCEvent& event) override;

    // Whether a path is a media file this viewer can display.
    static bool IsSupportedMedia(const std::string& path);

private:
    void BuildUI(float w, float h);
    void LoadCurrent(bool animated);
    // Start playback of a freshly loaded (or already shown) video according
    // to videoPreviewMode; no-op when the video backend is unavailable.
    void ApplyVideoPreviewToCurrent();
    void StopVideoClipTimer();
    void UpdateBreadcrumb();          // rebuild the folder path strip from currentFolder
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
    void ShowView(MediaKind kind);    // toggle child visibility for the kind
    static bool IsDocumentFile(const std::string& path);   // PDF (and other docs)
    static bool IsSpreadsheetFile(const std::string& path); // ODS / CSV / TSV
    static bool IsModelFile(const std::string& path);       // STL 3D models
    static bool IsEBookFile(const std::string& path);       // EPUB / FB2 / MOBI / AZW
    static bool IsUCDFile(const std::string& path);         // UltraCanvas Document (*.ucd)
    // Bitmaps and vector graphics the image pipeline rasterizes (SVG through
    // librsvg). Checked before IsTextFile so markup-based image formats show
    // their picture rather than their source code.
    static bool IsImageFile(const std::string& path);
    static bool IsTextFile(const std::string& path);        // text / source / markdown
    static bool IsVideoFile(const std::string& path);
    static bool IsAudioFile(const std::string& path);
    static MediaKind ClassifyFile(const std::string& path);

    // ----- keyboard plumbing -----
    // Browsing / view keys, shared by OnEvent() (focus is on the widget or one
    // of its controls, so the event bubbles here) and by the window key filter
    // (focus is nowhere, or on a display view that would swallow the key).
    bool HandleViewerKey(const UCEvent& event);
    bool HandleFilteredKey(const UCEvent& event);
    void InstallKeyFilter();
    void RemoveKeyFilter();
    std::string KeyFilterId() const;
    // The child element showing the current file (surface, PDF view, …).
    UltraCanvasUIElement* ActiveViewElement() const;
    // True when `element` is one of the display views (not a toolbar control).
    bool IsDisplayView(const UltraCanvasUIElement* element) const;
    // Visible with every ancestor visible (IsVisible() is per-element only).
    bool IsEffectivelyVisible() const;
    // The active view uses the bare arrow keys itself (spreadsheet cells), so
    // browsing needs the Alt modifier while it is showing.
    bool ActiveViewUsesArrowKeys() const { return activeKind == MediaKind::Sheet; }
    std::shared_ptr<UltraCanvasUIElement> BuildAdjustSlider(
            const std::string& id, const std::string& caption,
            float minV, float maxV, float value, std::function<void(float)> onChange);
    static std::vector<std::string> EnumerateFolder(const std::string& folder);

    std::vector<std::string> playlist;
    size_t currentIndex = 0;
    std::string currentFolder;     // folder the breadcrumb reflects
    bool topBarsVisible = true;    // breadcrumb + toolbars above the surface

    std::shared_ptr<UltraCanvasBreadcrumb>   breadcrumb;
    std::shared_ptr<UltraCanvasToolbar>      toolbar;   // navigation / slideshow row
    std::shared_ptr<UltraCanvasToolbar>      toolbar2;  // view / edit row
    std::shared_ptr<UltraCanvasContainer>    adjustPanel;
    std::shared_ptr<UltraCanvasMediaSurface> surface;
    std::shared_ptr<UltraCanvasContainer>    bottomBar;
    std::shared_ptr<UltraCanvasLabel>        infoLabel;
    std::shared_ptr<UltraCanvasButton>       playButton;
    // Alternate views shown instead of the image surface, depending on the
    // current file's kind. Held as the base type so the public header need not
    // include the PDF / audio / video plugins; each is only constructed when its
    // backend is compiled in. `activeKind` marks which view is live.
    std::shared_ptr<UltraCanvasUIElement>    pdfView;       // UltraCanvasPDFView
    std::shared_ptr<UltraCanvasUIElement>    sheetView;     // UltraCanvasSpreadsheet
    std::shared_ptr<UltraCanvasUIElement>    modelView;     // UltraCanvasSTLElement (3D)
    std::shared_ptr<UltraCanvasUIElement>    textView;      // UltraCanvasTextArea (read-only)
    std::shared_ptr<UltraCanvasUIElement>    bookView;      // UltraCanvasEBookViewer
    std::shared_ptr<UltraCanvasUIElement>    videoPlayer;   // UltraCanvasVideoPlayerElement
    std::shared_ptr<UltraCanvasUIElement>    audioPlayer;   // UltraCanvasAudioPlayerElement
    MediaKind activeKind = MediaKind::Image;

    // Details text for the current UCD container (empty when the current file
    // is not a *.ucd). Feeds the info popup instead of the image/text details.
    std::string ucdDetails;

    MediaAdjustments adjustments;

    bool   slideshowPlaying = false;
    double slideshowIntervalSec = 5.0;
    TimerId slideshowTimer = 0;

    VideoPreviewMode videoPreviewMode = VideoPreviewMode::Autoplay;
    float videoPreviewClipSec = 5.0f;
    TimerId videoClipTimer = 0;        // ends the muted PreviewClip playback
    MediaTransition transition = MediaTransition::CrossFade;
    int transitionDurationMs = 450;

    bool grabFocusOnAttach = true;   // claim the keyboard when attached to a window
    bool keyFilterInstalled = false; // window key filter is live
};

// ===== FACTORY =====
inline std::shared_ptr<UltraCanvasMediaViewer> CreateMediaViewer(
        const std::string& identifier, float x, float y, float w, float h) {
    return std::make_shared<UltraCanvasMediaViewer>(identifier, x, y, w, h);
}

} // namespace UltraCanvas
