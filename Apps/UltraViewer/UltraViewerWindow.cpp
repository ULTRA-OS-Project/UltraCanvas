// Apps/UltraViewer/UltraViewerWindow.cpp
// UltraViewer main window: hosts a full-window UltraCanvasMediaViewer and
// feeds it the command-line paths. Everything else — browsing, slideshow,
// zoom, image adjustments, the per-kind display views and the audio/video
// player transport controls — lives in the viewer widget.
// Version: 1.0.0
// Last Modified: 2026-08-06
// Author: UltraCanvas Framework

#include "UltraViewerWindow.h"

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace UltraCanvas {

bool UltraViewerWindow::Initialize(const std::vector<std::string>& paths) {
    WindowConfig config;
    config.title = "UltraViewer";
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
    if (paths.size() == 1) {
        std::error_code ec;
        if (fs::is_directory(paths.front(), ec)) viewer->OpenFolder(paths.front());
        else                                     viewer->OpenFile(paths.front());
    } else if (paths.size() > 1) {
        viewer->SetFiles(paths, 0);
    }

    return true;
}

void UltraViewerWindow::Show() {
    if (window) window->Show();
}

} // namespace UltraCanvas
