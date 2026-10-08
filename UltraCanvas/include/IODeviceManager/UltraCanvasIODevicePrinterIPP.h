// include/IODeviceManager/UltraCanvasIODevicePrinterIPP.h
// Registration for the IPP driverless printer backend.
//
// The backend itself is internal; this is only what
// UltraCanvasIODeviceBackends.cpp needs to attach it, and the one query the
// Windows spooler backend borrows.
// Version: 0.3.0
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

// Asks the printer at `printerUri` (ipp://, ipps://, http:// or https://) for
// its supply levels with one Get-Printer-Attributes. Fails when the printer
// cannot be reached, does not answer IPP there, or refuses; succeeds with an
// empty list when it answers but reports no levels. Blocks for up to the
// metadata timeout (10 s; 5 s to connect), so call it off the UI thread.
IODeviceResult QueryIppSupplyLevels(const std::string& printerUri,
                                    std::vector<IOSupplyLevel>& outSupplies);

#if defined(_WIN32)
// The hosts the machine's Windows print queues reach their printers at:
// IPP ports, Standard TCP/IP ports, and WSD ports through the device Plug and
// Play keeps beside the queue. Lives in the Windows spooler backend; the IPP
// backend asks it so a printer it finds over DNS-SD that is already a queue
// is left to the spooler (IppPrinterIsWindowsQueue).
std::vector<std::string> WindowsQueuePrinterHosts();
#endif
#endif

}  // namespace Internal
}  // namespace UltraCanvas
