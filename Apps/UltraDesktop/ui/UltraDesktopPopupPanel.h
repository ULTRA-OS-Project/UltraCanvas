// Apps/UltraDesktop/ui/UltraDesktopPopupPanel.h
// What the bars' small panels share: an undecorated window of its own beside
// the bar (the desktop's window lies under every other, so a popup inside it
// would be covered by whatever window sits there), placed by its top-right
// corner in screen pixels and kept on the screen, closed when it loses the
// focus, on Escape, and on request. The clock panel and the volume panel
// are built on it; the clipboard panel predates it and keeps its own copy
// of the arrangement, since its keyboard handling is the larger part.
// Version: 0.1.1 - SetScreenArea: kept on the desktop's own monitor
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasWindow.h"

#include <memory>
#include <string>

namespace UltraCanvas {
    struct UCEvent;
}

namespace UltraDesktop {

class UltraDesktopPopupPanel {
public:
    UltraDesktopPopupPanel(std::string title, int width, int height);
    virtual ~UltraDesktopPopupPanel();

    // `rightX`, `topY`: the panel's top-right corner in screen pixels, which
    // the bar hands over from its button; a corner off the screen is moved
    // onto it. An open panel is raised, not opened twice.
    void Open(int rightX, int topY);
    void Close();
    bool IsOpen() const { return open_ && window_; }
    // The monitor the panel stays on, in screen pixels: the desktop's own.
    // Unset, the whole screen.
    void SetScreenArea(int x, int y, int width, int height);

protected:
    // The window is empty and sized; add the elements. Called once per Open.
    virtual void Build(UltraCanvas::UltraCanvasWindow& window) = 0;
    // The window is about to go: drop the shared_ptrs into it.
    virtual void Unbuild() {}
    // Escape closes; a panel may take other keys first.
    virtual bool OnKey(const UltraCanvas::UCEvent& event);
    // Closing from inside the window's own event waits for that event to end.
    void CloseSoon();

    UltraCanvas::UltraCanvasWindow* Window() const { return window_.get(); }

private:
    std::string title_;
    int width_;
    int height_;
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    int areaX_ = 0, areaY_ = 0, areaW_ = 0, areaH_ = 0;
    bool open_ = false;
    bool closing_ = false;
};

} // namespace UltraDesktop
