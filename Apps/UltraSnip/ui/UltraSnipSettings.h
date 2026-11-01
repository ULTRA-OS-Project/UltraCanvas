// Apps/UltraSnip/ui/UltraSnipSettings.h
// What UltraSnip remembers between starts, in the framework's per-application
// settings file (UltraCanvasAppSettings: <settings folder>/UltraSnip/
// settings.json): the last shape, kind and delay, whether a snip is saved
// and copied by itself, the recording's frame rate, and the folder Save as
// used last.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraSnipTypes.h"

#include <string>

namespace UltraSnip {

struct UltraSnipSettings {
    SnipMode mode = SnipMode::Rectangle;
    SnipKind kind = SnipKind::Picture;
    int delaySeconds = 0;
    bool autoSave = true;           // every snip goes to Pictures/Screenshots (a recording always has its file)
    bool autoCopy = true;           // every picture goes to the clipboard
    int frameRate = 20;             // of a recording, 5..60
    std::string saveFolder;         // where Save as starts; "" = the default folder

    void Load();
    void Save() const;
};

} // namespace UltraSnip
