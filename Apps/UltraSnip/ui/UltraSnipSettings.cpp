// Apps/UltraSnip/ui/UltraSnipSettings.cpp
// See the header.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraSnipSettings.h"

#include "UltraCanvasAppSettings.h"

#include <algorithm>

using namespace UltraCanvas;

namespace UltraSnip {

namespace {
const char* kApp = "UltraSnip";
}

void UltraSnipSettings::Load() {
    UltraCanvasAppSettings file(kApp);
    if (!file.Load()) return;
    mode = ModeFromKey(file.GetString("snip.mode", ModeKey(mode)), mode);
    kind = KindFromKey(file.GetString("snip.kind", KindKey(kind)), kind);
    delaySeconds = file.GetInt("snip.delay", delaySeconds);
    if (delaySeconds != 0 && delaySeconds != 3 && delaySeconds != 5 && delaySeconds != 10) delaySeconds = 0;
    autoSave = file.GetBool("save.automatic", autoSave);
    autoCopy = file.GetBool("save.copy", autoCopy);
    frameRate = std::clamp(file.GetInt("video.frameRate", frameRate), 5, 60);
    saveFolder = file.GetString("save.folder", saveFolder);
}

void UltraSnipSettings::Save() const {
    UltraCanvasAppSettings file(kApp);
    file.Load();                                  // keep what another version wrote
    file.Set("snip.mode", ModeKey(mode));
    file.Set("snip.kind", KindKey(kind));
    file.Set("snip.delay", delaySeconds);
    file.Set("save.automatic", autoSave);
    file.Set("save.copy", autoCopy);
    file.Set("video.frameRate", frameRate);
    file.Set("save.folder", saveFolder);
    file.Save();
}

} // namespace UltraSnip
