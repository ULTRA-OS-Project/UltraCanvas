// Apps/UltraSnip/ui/UltraSnipWindow.h
// UltraSnip's window: the bar with New, the shape (rectangle, window, whole
// screen), the kind (picture, video) and the delay; the last snip below it
// (the picture in an UltraCanvasImageElement, a recording in the media
// viewer); and under that Save as, Copy, Open folder, Edit in UltraPaint and
// the options. The window hides itself while a snip is taken, so it is never
// in the picture, and comes back with the result.
// Version: 0.2.0 - the settings window; pictures written in the chosen type and folder;
//                  the window stays hidden after a desktop snip when asked
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraSnipCapture.h"
#include "UltraSnipOverlay.h"
#include "UltraSnipRecorder.h"
#include "UltraSnipRecordingBar.h"
#include "UltraSnipSettings.h"
#include "UltraSnipTypes.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {
class UltraCanvasButton;
class UltraCanvasContainer;
class UltraCanvasDropdown;
class UltraCanvasImageElement;
class UltraCanvasLabel;
class UltraCanvasMediaViewer;
class UltraCanvasSegmentedControl;
}

namespace UltraSnip {

class UltraSnipWindow {
public:
    explicit UltraSnipWindow(UltraCanvas::UltraCanvasApplication& app);
    ~UltraSnipWindow();

    bool Create();
    void Show();
    // A request from the command line, the desktop or a second start: a
    // capture at once, or just the window forward
    void Present(const SnipRequest& request);
    // Begins a snip: the window hides, the delay passes, the screen is taken
    void StartCapture(const SnipRequest& request);

private:
    enum class ResultKind { Nothing, Picture, Video };

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildToolbar();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildContent();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildActions();
    void ApplySettingsToControls();
    SnipRequest RequestFromControls() const;

    // The capture, step by step
    void TakeScreen();
    void OnAreaChosen(const UltraCanvas::Rect2Di& area);
    void OnCaptureCancelled();
    void FinishPicture(const UltraCanvas::Rect2Di& area);
    void BeginRecording(const UltraCanvas::Rect2Di& area);
    void StopRecording();
    void OnRecordingFailed(const std::string& reason);
    void TickRecording();
    void ShowWindowAgain();
    void Fail(const std::string& message);

    // The result
    void ShowPicture();
    void ShowVideo(const std::string& path);
    void ShowNothing(const std::string& message);
    void UpdateActions();
    void SetStatus(const std::string& text, bool error = false);
    bool CopyPictureToClipboard();
    bool SavePictureTo(const std::string& path, std::string& error);
    void SaveAs();
    void OpenFolder();
    void EditInPaint();
    void OpenSettings();
    bool OnKey(const UltraCanvas::UCEvent& event);

    UltraCanvas::UltraCanvasApplication& app_;
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    UltraSnipSettings settings_;

    // The bar
    std::shared_ptr<UltraCanvas::UltraCanvasButton> newButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasSegmentedControl> modes_;
    std::shared_ptr<UltraCanvas::UltraCanvasSegmentedControl> kinds_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> delays_;
    // The result
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> content_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> welcome_;
    std::shared_ptr<UltraCanvas::UltraCanvasImageElement> picture_;
    std::shared_ptr<UltraCanvas::UltraCanvasMediaViewer> viewer_;
    // The actions and options
    std::shared_ptr<UltraCanvas::UltraCanvasButton> saveButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> copyButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> folderButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> paintButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> settingsButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> status_;

    // A capture in flight
    SnipRequest pending_;
    bool capturing_ = false;
    bool windowWasShown_ = false;
    UltraCanvas::TimerId delayTimer_ = 0;
    UltraCanvas::DesktopScreenImage shot_;              // the frozen screen
    UltraCanvas::Rect2Di screenBounds_;                 // where the shot lies
    std::unique_ptr<UltraSnipOverlay> overlay_;
    std::unique_ptr<UltraSnipRecorder> recorder_;
    std::unique_ptr<UltraSnipRecordingBar> recordingBar_;
    UltraCanvas::TimerId recordingTimer_ = 0;

    // The result
    ResultKind result_ = ResultKind::Nothing;
    UltraCanvas::ClipboardDib::RgbaImage resultImage_;  // the picture
    std::vector<uint8_t> resultPng_;                    // ... as PNG
    std::string resultPath_;                            // where it is on disk; "" when unsaved
};

} // namespace UltraSnip
