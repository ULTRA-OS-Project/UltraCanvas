// OS/Linux/UltraCanvasLinuxSystemLanguage.cpp
// SystemLanguageTagsNative on Linux and the BSDs: nothing beyond the POSIX
// variables, which are the desktop's own answer there - GNOME, KDE and the
// display managers export LANG / LANGUAGE for the session from the language
// the person chose. UltraCanvasSystemLanguage.cpp reads those.
// Version: 1.0.0
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework
#include "UltraCanvasSystemLanguage.h"

namespace UltraCanvas {

std::vector<std::string> SystemLanguageTagsNative() {
    return {};
}

}  // namespace UltraCanvas
