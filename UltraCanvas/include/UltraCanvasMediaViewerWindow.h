// include/UltraCanvasMediaViewerWindow.h
// One file, full size, in its own window: an UltraCanvasMediaViewer filling a
// top-level window, opened over the window the user is looking at.
//
// The companion to a preview pane. A pane sized to the side of a file manager
// answers "is this the file I meant"; this answers "let me actually look at
// it" - which for a font is the difference between a two-letter specimen and
// every glyph the file contains, and for a spreadsheet or a document the
// difference between a thumbnail and the thing itself.
//
// It shows whatever UltraCanvasMediaViewer shows, so it needs no per-format
// case of its own: images, video, audio, documents, spreadsheets, e-books, 3D
// models and fonts all open the same way, and the arrow keys walk the rest of
// the folder. UltraCanvasImageViewer remains the right choice for a picture
// that wants zoom and pan over a dark lightbox; this is the general one.
// Version: 1.0.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASMEDIAVIEWERWINDOW_H
#define ULTRACANVASMEDIAVIEWERWINDOW_H

#include "UltraCanvasMediaViewer.h"
#include "UltraCanvasWindow.h"

#include <memory>
#include <string>

namespace UltraCanvas {

    // ===== HOW THE WINDOW OPENS =====
    struct MediaViewerWindowOptions {
        // Window title. Empty means the file's own name, which is what a
        // viewer window is normally called.
        std::string title;
        // Browse the rest of the folder (arrow keys, Next / Previous) instead
        // of showing the one file alone. On by default: having opened one font
        // in a folder of fonts, the next one is one key away.
        bool browseFolder = true;
        // Size used when there is no host window to match.
        int width = 1040;
        int height = 720;
        Color background = Color(24, 24, 28, 255);
    };

    // ===== THE VIEWER WINDOW =====
    // One instance owns at most one window: showing another file reuses it,
    // the way UltraCanvasImageViewer does, so repeated double-clicks do not
    // litter the desktop. Closes on Escape.
    class UltraCanvasMediaViewerWindow {
    public:
        UltraCanvasMediaViewerWindow() = default;
        ~UltraCanvasMediaViewerWindow();

        UltraCanvasMediaViewerWindow(const UltraCanvasMediaViewerWindow&) = delete;
        UltraCanvasMediaViewerWindow& operator=(const UltraCanvasMediaViewerWindow&) = delete;

        // Open filePath in a window of its own. When `host` is given the window
        // opens over it, at its size and position and owned by it, so it
        // appears where the user is looking. Returns false when the window
        // could not be created; a file the viewer cannot show still opens, and
        // says so in the window, because that is more use than nothing
        // happening - callers that want to decide first ask
        // UltraCanvasMediaViewer::IsSupportedMedia().
        bool Show(const std::string& filePath,
                  UltraCanvasWindowBase* host = nullptr,
                  const MediaViewerWindowOptions& options = {});
        void Close();
        bool IsOpen() const { return window != nullptr; }

        // The viewer inside, for a caller that wants to drive it - start a
        // slideshow, step to the next file - or null while nothing is open.
        UltraCanvasMediaViewer* GetViewer() const { return viewer.get(); }

    private:
        std::shared_ptr<UltraCanvasWindow> window;
        std::shared_ptr<UltraCanvasMediaViewer> viewer;
    };

} // namespace UltraCanvas

#endif // ULTRACANVASMEDIAVIEWERWINDOW_H
