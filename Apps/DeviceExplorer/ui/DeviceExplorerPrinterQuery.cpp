// Apps/DeviceExplorer/ui/DeviceExplorerPrinterQuery.cpp
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DeviceExplorerPrinterQuery.h"

#include "IODeviceManager/UltraCanvasIODevicePrinter.h"

#include <chrono>

using namespace UltraCanvas;

namespace DeviceExplorer {

PrinterStatusReport QueryPrinterStatus(IODeviceManager& manager, const IODeviceId& deviceId) {
    PrinterStatusReport report;
    report.queriedAt = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    auto printer = std::dynamic_pointer_cast<PrinterDevice>(manager.GetDeviceById(deviceId));
    if (!printer) {
        report.error = "The printer is no longer registered";
        return report;
    }

    const bool wasOpen = printer->IsConnected();
    if (!wasOpen) {
        const IODeviceResult opened = printer->Connect();
        if (!opened) {
            report.error = opened.message.empty()
                               ? std::string(IODeviceResultCodeToString(opened.code))
                               : opened.message;
            printer->Disconnect();   // DoConnect may have half-opened it
            return report;
        }
    }

    report.status = printer->GetStatus();
    // Some backends report supplies only through their own call rather than
    // inside the status; ask it when the status came back without any.
    if (report.status.supplies.empty()) report.status.supplies = printer->GetSupplyLevels();
    report.answered = true;

    if (!wasOpen) printer->Disconnect();
    return report;
}

PrinterStatusMap QueryAllPrinterStatus(IODeviceManager& manager) {
    PrinterStatusMap statuses;
    for (const IODevicePtr& device : manager.GetDevices(IODeviceCategory::Printer)) {
        if (!device) continue;
        const IODeviceId id = device->GetDeviceId();
        statuses[id] = QueryPrinterStatus(manager, id);
    }
    return statuses;
}

} // namespace DeviceExplorer
