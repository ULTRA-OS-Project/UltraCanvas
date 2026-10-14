// OS/MSWindows/AppEntry/UltraCanvasWindowsAttachConsole.cpp
// Attaches a GUI-subsystem application to the console of the prompt it was
// started from, before main() runs.
// Version: 1.0.0
// Author: UltraCanvas Framework
//
// Compiled into each Windows GUI-subsystem executable by
// ultracanvas_windows_gui_app() (cmake/UltraCanvasWindowsGuiApp.cmake), never
// into the core library - which is why it lives in a folder of its own, out of
// reach of the core's OS/MSWindows/*.cpp glob. In a DLL, a static initialiser
// runs under the loader lock; in the executable it runs after every DLL is
// loaded and initialised.
//
// UltraCanvasWindowsApplication::InitializeNative() attaches as well, but only
// once the window is on its way: `--help`, `--version` and a bad argument are
// printed by main() before that, and without this they reached no console.
#include "../UltraCanvasWindowsDiagnostics.h"

namespace {

// A no-op when the process already has its streams (a console program, or a
// launcher that redirected them) and when it was started from Explorer.
[[maybe_unused]] const bool attachedBeforeMain = UltraCanvas::AttachParentConsole();

} // namespace
