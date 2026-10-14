// Apps/UltraViewer/UltraViewerWindow.cpp
// UltraViewer main window: hosts a full-window UltraCanvasMediaViewer and
// feeds it the command-line paths. Everything else — browsing, slideshow,
// zoom, image adjustments, the per-kind display views and the audio/video
// player transport controls — lives in the viewer widget.
// Version: 1.1.0 - ShowPaths / BringForward (what a second start hands over);
//                  a folder is told apart through PathFromUtf8
// Version: 1.0.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework

#include "UltraViewerWindow.h"
#include "UltraCanvasPathUtf8.h"

#include <filesystem>
#include <system_error>

// ULTRAVIEWER_VERSION comes from the build alone: CMake reads the first line of
// Docs/UltraViewer/CHANGELOG.md (cmake/UltraCanvasVersion.cmake) and passes it as a
// compile definition. No fallback, so a build that lost it fails instead of
// showing a wrong number in the window title.
#ifndef ULTRAVIEWER_VERSION
#error "ULTRAVIEWER_VERSION is not defined: build through CMake, which reads it from Docs/UltraViewer/CHANGELOG.md"
#endif

namespace fs = std::filesystem;

namespace UltraCanvas {

bool UltraViewerWindow::Initialize(const std::vector<std::string>& paths) {
    WindowConfig config;
    config.title = "UltraViewer " ULTRAVIEWER_VERSION;
    config.width = 1200;
    config.height = 800;
    config.resizable = true;
    config.type = WindowType::Standard;

    window = CreateWindow(config);
    if (!window || !window->IsCreated()) return false;

    window->layout.SetFlexColumn()
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    window->SetBackgroundColor(Color(24, 24, 28, 255));

    viewer = CreateMediaViewer("uvw-viewer", 0, 0, 0, 0);
    viewer->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    window->AddChild(viewer);

    // Open whatever the command line brought. The viewer handles the rest
    // (drag & drop, the Open dialog, arrow-key browsing).
    ShowPaths(paths);
    return true;
}

void UltraViewerWindow::ShowPaths(const std::vector<std::string>& paths) {
    if (!viewer) return;
    if (paths.size() == 1) {
        std::error_code ec;
        if (fs::is_directory(PathFromUtf8(paths.front()), ec)) viewer->OpenFolder(paths.front());
        else                                                   viewer->OpenFile(paths.front());
    } else if (paths.size() > 1) {
        viewer->SetFiles(paths, 0);
    }
}

void UltraViewerWindow::Show() {
    if (window) window->Show();
}

void UltraViewerWindow::BringForward() {
    if (!window) return;
    if (window->IsMinimized()) window->Restore();
    else if (!window->IsWindowVisible()) window->Show();
    window->RaiseAndFocus();
}

} // namespace UltraCanvas
