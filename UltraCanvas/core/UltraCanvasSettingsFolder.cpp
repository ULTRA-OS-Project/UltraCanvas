// core/UltraCanvasSettingsFolder.cpp
// See UltraCanvasSettingsFolder.h.
// Version: 1.1.0 - the rule moved into the header (UltraCanvasSettingsFolderRoot); this joins the folder
// Version: 1.0.1 - every variable read through GetEnvUtf8, HOME and XDG_CONFIG_HOME too
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasSettingsFolder.h"

namespace UltraCanvas {

    std::filesystem::path UltraCanvasSettingsFolder() {
        const std::filesystem::path root = UltraCanvasSettingsFolderRoot();
        if (root.empty()) return {};
        return root / "UltraCanvas";
    }

}  // namespace UltraCanvas
