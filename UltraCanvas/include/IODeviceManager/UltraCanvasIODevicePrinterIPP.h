// include/IODeviceManager/UltraCanvasIODevicePrinterIPP.h
// Registration for the IPP driverless printer backend.
//
// The backend itself is internal; this is only what
// UltraCanvasIODeviceBackends.cpp needs to attach it.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasIODevicePrinter.h"

namespace UltraCanvas {

class IODeviceManager;

namespace Internal {

#if defined(ULTRACANVAS_HAS_NET)
// Registers the IPP enumerator: DNS-SD discovery of _ipp._tcp and _ipps._tcp,
// plus any printer named in ULTRACANVAS_IPP_PRINTERS.
void RegisterIppPrinterBackend(IODeviceManager& manager);
#endif

}  // namespace Internal
}  // namespace UltraCanvas
