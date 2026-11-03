// Apps/UltraSnip/ui/UltraSnipRecordingBar.h
// The small bar that floats beside the area being recorded: the red mark
// and the elapsed time, Pause / Resume and Stop. A notification-type window
// (kept above everything, on every virtual desktop, never taking the
// keyboard), placed outside the recorded area where there is room, so it
// stays out of the recording.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasWindow.h"

#include <functional>
#include <memory>

namespace UltraCanvas {
class UltraCanvasButton;
class UltraCanvasLabel;
}

namespace UltraSnip {

class UltraSnipRecordingBar {
public:
    UltraSnipRecordingBar();
    ~UltraSnipRecordingBar();

    // Opens the bar beside `area` (screen pixels), on `screen`
    bool Open(const UltraCanvas::Rect2Di& area, const UltraCanvas::Rect2Di& screen);
    void Close();
    bool IsOpen() const { return window_ != nullptr; }

    // The elapsed time shown, and whether the recording is paused
    void SetElapsed(double seconds, bool paused);

    std::function<void()> onPause;
    std::function<void()> onResume;
    std::function<void()> onStop;

private:
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> mark_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> time_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> pause_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> stop_;
    bool paused_ = false;
};

} // namespace UltraSnip
