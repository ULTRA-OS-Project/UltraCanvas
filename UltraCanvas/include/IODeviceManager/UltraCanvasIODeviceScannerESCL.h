// include/IODeviceManager/UltraCanvasIODeviceScannerESCL.h
// Registration for the eSCL driverless scanner backend.
//
// The backend itself is internal; this is only what
// UltraCanvasIODeviceBackends.cpp needs to attach it.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasIODeviceScanner.h"

namespace UltraCanvas {

class IODeviceManager;

namespace Internal {

#if defined(ULTRACANVAS_HAS_NET)
// Registers the eSCL enumerator: mDNS discovery plus any scanner named in
// ULTRACANVAS_ESCL_SCANNERS.
void RegisterEsclScannerBackend(IODeviceManager& manager);
#endif

}  // namespace Internal
}  // namespace UltraCanvas
