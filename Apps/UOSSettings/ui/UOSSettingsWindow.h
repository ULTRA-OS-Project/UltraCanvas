// Apps/UOSSettings/ui/UOSSettingsWindow.h
// The UOS-Settings window, laid out like UltraFiler's settings: a tree of
// settings pages on the left, the selected page on the right, a Close button
// in the bar at the foot. Every change is written at once - there is no
// Apply - and takes effect the next time an application opens the setting.
//
// Pages:
//   File dialogs > Last used folder
//       One folder for every application (Global) or one per application
//       (Individual); under Individual a table lists the applications, each
//       with its own Global | Individual switch and the folder it opens in.
//
// Version: 0.1.0
// Last Modified: 2026-10-01
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasWindow.h"   // UltraCanvasWindow is a per-platform typedef

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace UltraCanvas {
    class UltraCanvasContainer;
    class UltraCanvasTreeView;
    class UltraCanvasSegmentedControl;
    class UltraCanvasLabel;
}

namespace UOSSettings {

class UOSSettingsWindow {
public:
    explicit UOSSettingsWindow(std::string version);
    ~UOSSettingsWindow();

    bool Create();
    void Show();

    // The window was closed.
    std::function<void()> onClosed;

private:
    void BuildLastFolderPage();
    void ShowPage(const std::string& pageId);
    // Re-reads FileDialog.conf and fills the switch and the table from it.
    void RefreshLastFolderPage();
    void SetMode(bool individual);
    void SetAppScope(const std::string& appName, bool individual);

    std::string version_;
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvas::UltraCanvasTreeView> tree_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> pageArea_;
    std::map<std::string, std::shared_ptr<UltraCanvas::UltraCanvasContainer>> pages_;

    // ----- Last used folder -----
    // One table row per application. Rows are made once and then updated in
    // place: a row's switch must not be destroyed by its own callback.
    struct AppRow {
        std::shared_ptr<UltraCanvas::UltraCanvasLabel> folder;
        std::shared_ptr<UltraCanvas::UltraCanvasSegmentedControl> scope;
    };
    void AddAppRow(const std::string& appName);

    std::shared_ptr<UltraCanvas::UltraCanvasSegmentedControl> modeSwitch_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> tableRows_;   // scrolls
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> tableNote_;
    std::map<std::string, AppRow> rows_;
    bool refreshing_ = false;   // filling the controls, not the user changing them
};

} // namespace UOSSettings
