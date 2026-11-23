# UltraCanvas Screen Area Picker Documentation

## Overview

**UltraCanvasScreenAreaPicker** lets a person choose an area of the screen:
the screen, captured a moment before, is shown frozen in a borderless window
that covers it, dimmed, and the person drags a rectangle over it or clicks a
window. The answer is a rectangle in screen pixels; the pixels of it come
from the same capture (`ScreenArea::CropToRgba`). UltraSnip takes its
screenshots and recordings with it, and ChatCube's screenshot button puts the
chosen area into the message being written.

**File Location**: `include/UltraCanvasScreenAreaPicker.h`, `core/UltraCanvasScreenAreaPicker.cpp`
**Version**: 1.0.0 - moved here from UltraSnip (its overlay and capture helpers)
**Author**: UltraCanvas Framework

## Features

- ✅ The frozen screen, dimmed, with the dragged rectangle in full colour and
  its size in pixels beside it; a crosshair under the pointer
- ✅ **Window** mode: the window under the pointer is lit (the rest darker
  still) with its title and size; a click takes it, a drag still takes a
  rectangle
- ✅ Keys: **Enter** or **Space** takes the window under the pointer (Window
  mode) or the whole screen; **Escape** or a right click gives up
- ✅ The pure arithmetic in `ScreenArea` - the rectangle between two drag
  corners, a clip, the crop into straight RGBA, the topmost window under a
  point - usable and testable without a window
- ✅ macOS: placed in points (the capture is in pixels), above the menu bar
  and the Dock, with the keyboard (`WindowType::Overlay`)

## Quick Start

Hide your own window first if it is not to be in the picture, and give the
window system a moment to take it off the screen (UltraSnip and ChatCube
wait 350 ms). Then capture, open the picker on the capture, and crop:

```cpp
#include "UltraCanvasApplication.h"
#include "UltraCanvasClipboardDib.h"
#include "UltraCanvasDesktopShell.h"
#include "UltraCanvasScreenAreaPicker.h"

#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

class ScreenshotTaker {
public:
    // Call when the program's own window has left the screen
    bool Start(std::string& error) {
        if (!UltraCanvasDesktopShell::CanCaptureScreen()) {
            error = "This system cannot capture the screen.";
            return false;
        }
        if (!UltraCanvasDesktopShell::CaptureScreenImage(shot_, &error)) return false;
        auto* app = UltraCanvasApplicationBase::GetCurrent();
        std::vector<ScreenAreaWindow> windows = ScreenArea::ListPickableWindows();
        picker_ = std::make_unique<UltraCanvasScreenAreaPicker>();
        picker_->SetTitle("My program");
        // Called once the picker has closed; the picker is let go of from
        // the UI thread's queue, never from inside its own callback
        picker_->onSelected = [this, app](const Rect2Di& area) {
            app->PostToUIThread([this, area]() {
                picker_.reset();
                ClipboardDib::RgbaImage image;
                std::vector<uint8_t> png;
                if (ScreenArea::CropToRgba(shot_, area, image) && ClipboardDib::EncodePng(image, png)) {
                    // ... write png to a file, put it on the clipboard, ...
                }
                shot_ = DesktopScreenImage();
            });
        };
        picker_->onCancelled = [this, app]() {
            app->PostToUIThread([this]() {
                picker_.reset();
                shot_ = DesktopScreenImage();
            });
        };
        const ScreenAreaMode mode = windows.empty() ? ScreenAreaMode::Rectangle : ScreenAreaMode::Window;
        return picker_->Open(shot_, mode, windows, &error);
    }

private:
    DesktopScreenImage shot_;
    std::unique_ptr<UltraCanvasScreenAreaPicker> picker_;
};
```

## Key API

### `UltraCanvasScreenAreaPicker`

| Call | Meaning |
|---|---|
| `SetTitle(title)` | The picker window's title where the window system shows one (a task switcher): the program's name. Before `Open`. |
| `Open(shot, mode, windows, &error)` | Show `shot` (a `DesktopScreenImage` from `UltraCanvasDesktopShell::CaptureScreenImage`) over the screen and let the person choose. `windows` (screen pixels, bottom first) is what Window mode offers; empty, a click takes nothing and only rectangles are chosen. False with the reason when the picture could not be shown. |
| `Close()` | Take the picker down without calling either callback. |
| `SetMode(mode)` | The shape while it is open: `ScreenAreaMode::Rectangle` or `ScreenAreaMode::Window` (`FullScreen` is for the caller, which then takes the whole capture without the picker). |
| `SetHintTop(top)` | Where the hint line starts from the top, in logical pixels - lower while something of the caller's sits over the top of the screen (UltraSnip's bar). |
| `IsOpen()` | Whether it is on the screen. |
| `onSelected(area)` | The chosen rectangle in screen pixels, as `ListScreens` and the capture count them. Called after the picker window has closed. |
| `onCancelled()` | Escape, a right click, or the window closed. |
| `onReleased()` | A button went up over the picker while it is still open: a window kept above it (UltraSnip's bar) raises itself again. |

### `ScreenArea`

| Call | Meaning |
|---|---|
| `RectBetween(a, b)` | The rectangle between two drag corners, either way round, both corners inside it (a click is 1 x 1). |
| `ClipRect(rect, bounds)` | `rect` cut down to `bounds`; width or height 0 when they do not meet. |
| `CropToRgba(shot, area, out, &taken)` | `area` (screen pixels) out of `shot` as straight, opaque RGBA - what `ClipboardDib::EncodePng`, the clipboard and the image element take. Clipped to the shot; `taken` says what was taken. False when nothing of it is in the shot. |
| `TopmostWindowAt(windows, point)` | The index of the topmost window under `point` (the list bottom first); -1 for none. |
| `ListPickableWindows()` | The windows Window mode offers: `UltraCanvasDesktopShell::ListWindows` without the minimized ones and those kept out of the taskbar, with their bounds, bottom first. Empty where the platform lists no windows (Wayland, macOS). |

### `UltraCanvasScreenAreaSelection`

The marquee element the picker lays over the frozen screen: the dimming, the
dragged rectangle and its size, the crosshair, the lit window and the hint.
The picker builds it; it is public for a caller that shows a capture in a
window of its own.

## Platforms

| Platform | Placement |
|---|---|
| Linux (X11), BSD, Windows | A `WindowType::Borderless` window at the capture's position and size, in physical pixels. |
| Linux (Wayland) | The same; the capture comes from the desktop portal and there is no window list, so rectangles only. |
| macOS | A `WindowType::Overlay` window - borderless, above the menu bar and the Dock, on every Space, given the keyboard - placed and sized in points: the capture's pixels over the window's backing scale. The capture is the main display ([UltraCanvasDesktopShell](UltraCanvasDesktopShell.md)). |
| Android, WebAssembly | No capture (`CanCaptureScreen()` false). |

## Notes

- Hide your windows before the capture and show them again in both
  callbacks: the picker shows only what was captured.
- The capture can be large (a 4K screen is 33 MB): let go of it
  (`shot = DesktopScreenImage()`) once the area is cropped.
- The hint texts go through `Tr` and are in the standard catalogue
  (`media/locale/en/Standard.json`).
