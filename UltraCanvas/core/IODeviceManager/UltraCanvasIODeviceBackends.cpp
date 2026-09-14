// core/IODeviceManager/UltraCanvasIODeviceBackends.cpp
// Attaches the compiled-in device backends to the manager.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraCanvasIODeviceBackends.h"

namespace UltraCanvas {
namespace Internal {

void RegisterCompiledBackends(IODeviceManager& manager) {
    (void)manager;

#if defined(ULTRACANVAS_HAS_CUPS) && (defined(__linux__) || defined(__APPLE__))
    RegisterCupsPrinterBackend(manager);
#endif

    // Still to come, each adding a guarded call here:
    //
    //   #if defined(ULTRACANVAS_HAS_V4L2)
    //       RegisterV4L2CameraBackend(manager);      // OS/Linux
    //   #endif
    //   #if defined(ULTRACANVAS_HAS_GPHOTO2)
    //       RegisterGPhoto2CameraBackend(manager);   // OS/Linux
    //   #endif
    //   #if defined(ULTRACANVAS_HAS_CUPS)
    //       RegisterCupsPrinterBackend(manager);     // OS/Linux, OS/MacOS
    //   #endif
    //   #if defined(ULTRACANVAS_HAS_GUTENPRINT)
    //       RegisterGutenPrintBackend(manager);      // core/ — all platforms
    //   #endif
    //
    // Two backends serving one category on one platform is the normal case
    // (V4L2 webcams and gphoto2 DSLRs are both cameras), which is why the
    // manager merges enumerators instead of exposing one enumerate call per
    // category.
}

} // namespace Internal
} // namespace UltraCanvas
