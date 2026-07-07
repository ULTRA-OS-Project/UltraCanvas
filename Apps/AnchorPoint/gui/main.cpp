// Apps/AnchorPoint/gui/main.cpp
// AnchorPoint - GUI application entry point.
// Version: 0.2.0
// Author: AnchorPoint
#include "AnchorPointApp.h"

#include <iostream>

#ifdef __linux__
  #include <X11/Xlib.h>
#endif

using namespace UltraCanvas;

int main() {
#ifdef __linux__
    XInitThreads();  // transfers run on worker threads; X11 must be thread-safe
#endif

    UltraCanvasApplication app;
    if (!app.Initialize("AnchorPoint")) {
        std::cerr << "Failed to initialize UltraCanvas.\n";
        return 1;
    }

    AnchorPoint::AnchorPointGui gui(app);
    if (!gui.Build()) {
        std::cerr << "Failed to build AnchorPoint window.\n";
        return 1;
    }
    gui.Show();

    app.Run();
    return 0;
}
