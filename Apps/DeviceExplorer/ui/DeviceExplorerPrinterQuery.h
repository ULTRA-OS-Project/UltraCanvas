// Apps/DeviceExplorer/ui/DeviceExplorerPrinterQuery.h
// Asks one printer for its status and supplies.
//
// Kept out of DeviceExplorerModel so the model stays free of PrinterDevice,
// whose implementation pulls in the whole print stack (renderers, rasteriser,
// transports): the model test links nothing but the IODeviceManager core, and
// this is the one place DeviceExplorer needs more.
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DeviceExplorerModel.h"

namespace DeviceExplorer {

// Blocking - a network printer can take seconds to answer, so the window
// runs it on a worker thread. Opens the printer only if it is not already
// open, and closes only what it opened, so a session another part of the
// application holds is left exactly as it was. Nothing is printed,
// configured or cancelled: the session is used to read and nothing else.
PrinterStatusReport QueryPrinterStatus(UltraCanvas::IODeviceManager& manager,
                                       const UltraCanvas::IODeviceId& deviceId);

// Every registered printer, one after another, for --list --details.
PrinterStatusMap QueryAllPrinterStatus(UltraCanvas::IODeviceManager& manager);

} // namespace DeviceExplorer
