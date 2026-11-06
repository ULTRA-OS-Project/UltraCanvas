// Apps/UltraDesktop/ui/UltraDesktopWindowPreviews.h
// The pictures of an application's windows beside its taskbar button: one
// tile per window - its icon and title, a picture of the window
// (UltraCanvasDesktopShell::CaptureWindowImage, taken on a worker thread so
// the bar does not stall), a close mark - and a click on a tile brings that
// window forward. A minimized window, which has no picture, shows its icon.
//
// The strip is a window of its own that never takes the keyboard focus
// (WindowType::Notification): it opens while the pointer rests on the
// button, and the window the person is typing in keeps the focus. The bar
// decides when it opens and closes (UltraDesktopWindow: a short rest on the
// button, a grace time after the pointer leaves both); onPointerInside and
// onPointerOutside tell it where the pointer is.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasDesktopShell.h"
#include "UltraCanvasWindow.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {
    class UltraCanvasContainer;
    class UltraCanvasImageElement;
    class UltraCanvasLabel;
    struct UCEvent;
}

namespace UltraDesktop {

class UltraDesktopPanelWork;

struct WindowPreviewItem {
    uint64_t id = 0;
    std::string title;
    std::string iconFile;   // "" for none: the application's letter
    std::string letter;     // shown when there is no icon and no picture
    bool active = false;
    bool minimized = false;
};

// Which side of the button the strip opens on: beside a bar on the left,
// below one on top, above one at the bottom.
enum class PreviewSide { Right, Down, Up };

class UltraDesktopWindowPreviews {
public:
    UltraDesktopWindowPreviews();
    ~UltraDesktopWindowPreviews();
    UltraDesktopWindowPreviews(const UltraDesktopWindowPreviews&) = delete;
    UltraDesktopWindowPreviews& operator=(const UltraDesktopWindowPreviews&) = delete;

    // Opens the strip for `items`, the windows of the group `key`, beside
    // the button whose rectangle `buttonX`, `buttonY`, `buttonW`, `buttonH`
    // is in screen pixels, kept inside `area` (the desktop's monitor).
    // `scale`: physical pixels per logical pixel. An open strip is replaced.
    void Show(const std::string& key, const std::vector<WindowPreviewItem>& items, int buttonX, int buttonY,
              int buttonW, int buttonH, PreviewSide side, int areaX, int areaY, int areaW, int areaH,
              float scale);
    void Close();
    bool IsOpen() const { return window_ != nullptr; }
    bool IsOpenFor(const std::string& key) const { return window_ && key_ == key; }
    const std::string& Key() const { return key_; }
    // The window ids the open strip shows, in its order.
    std::vector<uint64_t> WindowIds() const;

    // A tile was clicked: bring that window forward. The strip has closed.
    std::function<void(uint64_t windowId)> onChoose;
    // A tile's close mark: ask that window to close. The strip has closed.
    std::function<void(uint64_t windowId)> onCloseWindow;
    // The pointer came onto the strip / left it.
    std::function<void()> onPointerInside;
    std::function<void()> onPointerOutside;

private:
    struct Tile {
        WindowPreviewItem item;
        std::shared_ptr<UltraCanvas::UltraCanvasContainer> box;
        std::shared_ptr<UltraCanvas::UltraCanvasImageElement> picture;
        std::shared_ptr<UltraCanvas::UltraCanvasLabel> close;
    };
    bool OnEvent(const UltraCanvas::UCEvent& event);
    void Highlight(int index);
    int TileAt(float x, float y) const;
    bool OnCloseMark(int index, float x, float y) const;
    void CapturePictures(int pictureW, int pictureH);
    void Finish(uint64_t windowId, bool closeIt);

    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::string key_;
    std::vector<Tile> tiles_;
    int highlighted_ = -1;
    bool pointerInside_ = false;
    // Captures that finish after the strip closed (or was replaced) are
    // dropped: each Show starts a new generation.
    uint64_t generation_ = 0;
    std::unique_ptr<UltraDesktopPanelWork> work_;
};

} // namespace UltraDesktop
