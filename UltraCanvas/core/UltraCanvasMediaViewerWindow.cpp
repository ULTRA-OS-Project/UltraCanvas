// core/UltraCanvasMediaViewerWindow.cpp
// One file, full size, in its own window.
// Version: 1.0.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework

#include "UltraCanvasMediaViewerWindow.h"

#include <algorithm>
#include <filesystem>

namespace UltraCanvas {

    namespace fs = std::filesystem;

    UltraCanvasMediaViewerWindow::~UltraCanvasMediaViewerWindow() {
        Close();
    }

    void UltraCanvasMediaViewerWindow::Close() {
        // Let go of the file before the window goes: a document engine that
        // still holds it open blocks a rename on Windows, and the viewer's own
        // preview-pane contract says to close it explicitly.
        if (viewer) viewer->CloseFile();
        viewer.reset();
        if (window) {
            window->Close();
            window.reset();
        }
    }

    bool UltraCanvasMediaViewerWindow::Show(const std::string& filePath,
                                           UltraCanvasWindowBase* host,
                                           const MediaViewerWindowOptions& options) {
        // One window per instance: a second double-click replaces what is in
        // it rather than opening another one.
        Close();

        WindowConfig cfg;
        cfg.title = options.title.empty()
                            ? fs::path(filePath).filename().string()
                            : options.title;
        if (cfg.title.empty()) cfg.title = "Media";
        cfg.type = WindowType::Standard;
        cfg.resizable = true;
        cfg.backgroundColor = options.background;
        if (host) {
            // Over the host window, at its size, so it opens where the user is
            // looking rather than at the screen origin.
            cfg.width = std::max(640, static_cast<int>(host->GetWidth()));
            cfg.height = std::max(480, static_cast<int>(host->GetHeight()));
            int hx = 0, hy = 0;
            host->GetWindowPosition(hx, hy);
            cfg.x = hx;
            cfg.y = hy;
            cfg.parentWindow = host;
        } else {
            cfg.width = std::max(320, options.width);
            cfg.height = std::max(240, options.height);
        }

        window = CreateWindow(cfg);
        if (!window) return false;
        window->SetBackgroundColor(options.background);
        window->layout.SetFlexColumn()
                      .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

        viewer = CreateMediaViewer("MediaViewerWindowView", 0, 0,
                                   static_cast<float>(cfg.width),
                                   static_cast<float>(cfg.height));
        viewer->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                          .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        window->AddChild(viewer);

        // Escape closes, the way every other viewer window in the framework
        // behaves. The viewer installs its own key filter for the arrow keys,
        // so this only has to claim the one it does not want.
        window->eventCallback = [this](const UCEvent& event) {
            if (event.type == UCEventType::KeyUp &&
                event.virtualKey == UCKeys::Escape) {
                Close();
                return true;
            }
            return false;
        };

        window->Show();
        // Some X11 window managers ignore the create-time coordinates for a
        // top-level window, so put it over the host again now that it exists.
        if (host) window->SetWindowPosition(cfg.x, cfg.y);

        // After Show(): the viewer sizes its display to the bounds it has, and
        // it only has them once the window has laid out.
        if (options.browseFolder) viewer->OpenFile(filePath);
        else viewer->SetFiles({filePath}, 0);
        return true;
    }

} // namespace UltraCanvas
