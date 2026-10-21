// Apps/UltraViewer/UltraViewerWindow.cpp
// UltraViewer main window: hosts a full-window UltraCanvasMediaViewer and
// feeds it the command-line paths. Everything else — browsing, slideshow,
// zoom, image adjustments, the per-kind display views and the audio/video
// player transport controls — lives in the viewer widget.
// Version: 1.2.0 - the settings dialog behind the viewer's gear: how long before
//                  Unlock's passwords are forgotten (UltraViewerSettings)
// Version: 1.1.0 - ShowPaths / BringForward (what a second start hands over);
//                  a folder is told apart through PathFromUtf8
// Version: 1.0.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework

#include "UltraViewerWindow.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

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

    settings.Load();
    ApplySettings();
    viewer->SetSettingsAction([this]() { ShowSettings(); });

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

void UltraViewerWindow::ApplySettings() {
    if (!viewer) return;
    viewer->SetForgetPasswordsAfterIdle(std::chrono::minutes(settings.forgetPasswordsAfterMinutes));
}

void UltraViewerWindow::ShowSettings() {
    if (!window) return;
    struct Choice { const char* label; int minutes; };
    static const std::vector<Choice> kChoices = {
        {"1 minute", 1}, {"2 minutes", 2}, {"5 minutes", 5}, {"10 minutes", 10}, {"15 minutes", 15},
        {"30 minutes", 30}, {"1 hour", 60}, {"2 hours", 120}, {"Never", 0}};

    DialogConfig config;
    config.title = "UltraViewer settings";
    config.message = "A password-protected PDF or UltraCanvas image is opened with Unlock, and its "
                     "password is kept while the folder is browsed. When UltraViewer has had no key "
                     "press, click or mouse movement for the time below, the passwords are forgotten "
                     "and a file shown unlocked goes back behind Unlock. Locking the screen does the "
                     "same at once, whatever is chosen here.";
    config.dialogType = DialogType::Information;
    config.showIcon = false;
    config.buttons = DialogButtons::NoButtons;
    config.width = 480;
    config.height = 260;
    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    dialog->SetIconVisible(false);

    auto row = std::make_shared<UltraCanvasContainer>("uvw-set-forget.row", 0, 0, 440, 30);
    row->layout.SetFlexRow().SetFlexGap(12).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    auto label = CreateLabel("uvw-set-forget.label", 0, 0, 200, 24, "Forget passwords after:");
    label->layoutItem.SetFlexShrink(0);
    row->AddChild(label);
    auto dropdown = CreateDropdown("uvw-set-forget", 0, 0, 160, 26);
    // The choice nearest to what is stored - a value written by hand may
    // be none of them - with Never only for Never.
    size_t nearest = 0;
    int bestDistance = -1;
    for (size_t i = 0; i < kChoices.size(); ++i) {
        dropdown->AddItem(kChoices[i].label);
        const int stored = settings.forgetPasswordsAfterMinutes;
        if ((kChoices[i].minutes == 0) != (stored == 0)) continue;
        const int distance = std::abs(kChoices[i].minutes - stored);
        if (bestDistance < 0 || distance < bestDistance) {
            bestDistance = distance;
            nearest = i;
        }
    }
    dropdown->SetSelectedIndex(static_cast<int>(nearest), false);
    dropdown->onSelectionChanged = [this](int index, const DropdownItem&) {
        if (index < 0 || index >= static_cast<int>(kChoices.size())) return;
        settings.forgetPasswordsAfterMinutes = kChoices[static_cast<size_t>(index)].minutes;
        settings.Save();
        ApplySettings();
    };
    row->AddChild(dropdown);
    dialog->AddDialogElement(row);

    dialog->AddCustomButton("Close", DialogResult::OK, DialogButtonRole::Default);
    UltraCanvasDialogManager::ShowDialog(dialog, nullptr, window.get());
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
