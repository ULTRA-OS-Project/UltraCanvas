// Apps/AnchorPoint/gui/main.cpp
// AnchorPoint - GUI application entry point. It opens no files from the
// command line: when AnchorPoint is already running, a second start brings
// the running window forward and quits (UltraCanvasSingleInstance.h). The
// headless `anchorpoint-cli` (cli/main.cpp) is not limited to one copy.
// Version: 0.3.0 - one copy per user: a second start brings the running
//                  window forward
// Version: 0.2.0
// Author: AnchorPoint
#include "AnchorPointApp.h"

#include "UltraCanvasConfig.h"
#include "UltraCanvasSingleInstance.h"

#include <cstdlib>
#include <iostream>

#ifdef __linux__
  #include <X11/Xlib.h>
#endif

using namespace UltraCanvas;

int main() {
#ifdef __linux__
    XInitThreads();  // transfers run on worker threads; X11 must be thread-safe
#endif

    // One copy per user. Declared before the window and closed before it
    // goes, so no request reaches a window that is going.
    UltraCanvasSingleInstance instance("org.ultraos.anchorpoint", "AnchorPoint");
    const UltraCanvasSingleInstance::Start start = instance.Claim();
    if (start == UltraCanvasSingleInstance::Start::HandedOff) return EXIT_SUCCESS;   // the running copy came forward

    UltraCanvasApplication app;
    if (!app.Initialize("AnchorPoint")) {
        std::cerr << "Failed to initialize UltraCanvas.\n";
        return 1;
    }
    if (start == UltraCanvasSingleInstance::Start::AlreadyRunning)
        return UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "AnchorPoint");
    // One icon, everywhere the app is drawn: the window and its taskbar entry
    // read this file; the .ico in the Windows binary and the desktop entry's
    // theme icon come from the same media/appicon/AnchorPoint.svg
    // (CMakeLists.txt).
    app.SetDefaultWindowIcon(NormalizePath(GetResourcesDir() + "media/appicon/AnchorPoint.png"));

    AnchorPointApp::AnchorPointGui gui(app);
    if (!gui.Build()) {
        std::cerr << "Failed to build AnchorPoint window.\n";
        return 1;
    }
    gui.Show();

    instance.ListenRaise();   // a later start brings this window forward
    app.Run();
    instance.Close();
    return 0;
}
