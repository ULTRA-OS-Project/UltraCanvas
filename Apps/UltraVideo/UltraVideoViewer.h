// Apps/UltraVideo/UltraVideoViewer.h
// The viewer (Docs/Research/UltraVideoDesignProposal.md §6.3): the frame
// surface that shows the rendered frame at the playhead on the app ground
// with the title-safe area dashed, the Fit dropdown over it, and the
// transport under it - to start, back a frame, play, forward a frame, to
// end, the large timecode readout over the project length, the volume.
// Playback itself is the window's: it moves the playhead and the viewer
// shows what the engine renders for it.
// Version: 0.2.2 - SetProjectSize
// Version: 0.2.1 - SetFit
// Version: 0.2.0 - DraftPlay returns bool
// Version: 0.1.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework
#pragma once

#include "UltraVideoTheme.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasTimecodeField.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasVideoPlayerElement.h"
#include "VideoFX/VideoFX.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraVideo {

// ui-reuse-exempt: the viewer's frame surface shows a decoded RGBA frame from
// memory; it draws the frame and the title-safe guide and nothing else.
class FrameSurface : public UltraCanvas::UltraCanvasUIElement {
public:
    explicit FrameSurface(const std::string& id);
    void SetFrame(const VideoFX::VideoFXFrame& frame);
    void ClearFrame();
    bool HasFrame() const { return pixmap != nullptr; }
    static constexpr double kMargin = 12.0;   // the picture's distance from the surface's edges
    // 0 = fit the frame to the surface; else a percentage of the frame's own pixels
    void SetZoomPercent(double percent, bool fill = false);
    // The project's frame: its shape when no frame is shown, its pixels for a percent zoom
    void SetProjectSize(int width, int height) {
        if (width <= 0 || height <= 0) return;
        projectWidth = width;
        projectHeight = height;
        aspect = static_cast<double>(width) / height;
        RequestRedraw();
    }
    void SetColors(const UltraCanvas::Color& ground, const UltraCanvas::Color& guide, const UltraCanvas::Color& frameLine);
    void Render(UltraCanvas::IRenderContext* ctx, const UltraCanvas::Rect2Df& dirtyRect) override;

private:
    UltraCanvas::UCPixmapPtr pixmap;
    int frameWidth = 0;
    int frameHeight = 0;
    double zoomPercent = 0.0;
    bool fillSurface = false;
    double aspect = 16.0 / 9.0;
    int projectWidth = 0;
    int projectHeight = 0;
    UltraCanvas::Color ground{22, 22, 24};
    UltraCanvas::Color guide{255, 255, 255, 60};
    UltraCanvas::Color frameLine{80, 80, 84};
};

class Viewer {
public:
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build(const Palette& palette);
    void ApplyPalette(const Palette& palette);

    void ShowFrame(const VideoFX::VideoFXFrame& frame);
    void ClearFrame();
    void SetTime(double seconds, double total);      // the readout and the length
    void SetFrameRate(double frameRate);
    void SetProjectSize(int width, int height);   // the frame's shape, and what a percent zoom is of
    void SetFit(int index);                 // the Fit menu's choice: fit, fill, 50, 100, 200 %
    void SetPlaying(bool playing);
    // The size to ask the engine for: the surface in logical pixels
    int SurfaceWidth() const;
    int SurfaceHeight() const;

    // Playback with sound: the draft render plays in the framework's player
    // element in the surface's place (proposal §5.5 level 3). The window
    // drives it and polls its position; the player's own callbacks fire off
    // the UI thread and are not used.
    bool ShowDraft(const std::string& path);         // false when the player cannot open it
    void HideDraft();                                // back to the frame surface
    bool DraftShown() const { return draftShown; }
    UltraCanvas::VideoPlaybackState DraftState() const;
    bool DraftPlay();                 // false when the player could not start
    void DraftPause();
    void DraftSeek(double seconds);
    double DraftPosition() const;

    std::function<void()> onToStart;
    std::function<void()> onStepBack;
    std::function<void()> onPlayPause;
    std::function<void()> onStepForward;
    std::function<void()> onToEnd;
    std::function<void(double)> onVolumeChanged;    // 0..1

private:
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> panel;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> top;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> fit;
    std::shared_ptr<FrameSurface> surface;
    std::shared_ptr<UltraCanvas::UltraCanvasVideoPlayerElement> player;
    int projectWidth = 0;
    int projectHeight = 0;
    double zoomPercent = 0.0;             // the Fit menu's percent, 0 = fit or fill
    bool draftShown = false;
    float volumeLevel = 1.0f;
    std::shared_ptr<UltraCanvas::UltraCanvasToolbar> transport;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> toStart, stepBack, play, stepForward, toEnd;
    std::shared_ptr<UltraCanvas::UltraCanvasTimecodeField> readout;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> total;
    std::shared_ptr<UltraCanvas::UltraCanvasSlider> volume;
    double frameRate = 25.0;
    bool playing = false;
};

} // namespace UltraVideo
