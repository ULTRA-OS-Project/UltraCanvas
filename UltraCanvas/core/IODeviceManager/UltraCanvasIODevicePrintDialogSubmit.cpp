// core/IODeviceManager/UltraCanvasIODevicePrintDialogSubmit.cpp
// The half of the print-dialog bridge that talks to the registry and to the
// dialog: find the printer the user named, open it, print, close it again.
//
// Separate from UltraCanvasIODevicePrintDialog.cpp because this one names
// UltraCanvasNativeDialogs::RequestPrintSettings(), and a reference to that
// drags a platform dialog implementation into the link line of anything that
// links this file. The matching and job-building next door stay free of it,
// which is what lets the printer tests cover them.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "../../include/IODeviceManager/UltraCanvasIODevicePrintDialog.h"

#include "../../include/IODeviceManager/UltraCanvasIODeviceManager.h"
#include "../../include/UltraCanvasNativeDialogs.h"

namespace UltraCanvas {

// ============================================================================
// PRINTING
// ============================================================================

IODeviceResult PrintTextWithSettings(const IOPrintDialogChoice& chosen,
                                     const std::string& documentName,
                                     const std::string& textContent) {
    if (!chosen.IsOK()) {
        return IODeviceResult::Error(IODeviceResultCode::Cancelled,
                                     "The print dialog was cancelled");
    }

    if (chosen.printToFile) {
        // Refused by name rather than silently spooled to whatever queue the
        // name happens to match: the user asked for a file, and quietly
        // printing paper instead is the worse of the two wrong answers.
        return IODeviceResult::Error(
            IODeviceResultCode::NotSupported,
            "The dialog's \"Print to File\" destination is not wired up yet; "
            "the job was not sent to a printer instead");
    }

    if (chosen.printerName.empty()) {
        return IODeviceResult::Error(IODeviceResultCode::InvalidArgument,
                                     "The print dialog named no printer");
    }

    IODeviceManager& manager = IODeviceManager::GetInstance();

    // Only when the registry has nothing: a rescan drops and rebuilds nothing
    // for a device still present, but it does talk to every backend, and a
    // caller that already enumerated should not pay for that again.
    if (manager.GetDeviceCount(IODeviceCategory::Printer) == 0) {
        manager.EnumerateDevices(IODeviceCategory::Printer);
    }

    PrinterDevicePtr printer =
        MatchPrinterByName(manager.GetDevices(IODeviceCategory::Printer),
                           chosen.printerName);
    if (!printer) {
        return IODeviceResult::Error(
            IODeviceResultCode::DeviceNotFound,
            "The print dialog chose '" + chosen.printerName +
                "', which IODeviceManager does not list as a printer");
    }

    // Connected only if it was not already, and closed again only in that
    // case: an application holding an open session on this printer - watching
    // its supply levels, say - would otherwise have it shut underneath.
    const bool wasConnected = printer->IsConnected();
    if (!wasConnected) {
        IODeviceResult opened = printer->Connect();
        if (!opened.success) {
            return opened;
        }
    }

    IODeviceResult printed =
        printer->Print(MakeTextPrintJob(chosen, documentName, textContent));

    if (!wasConnected) {
        printer->Disconnect();
    }
    return printed;
}

IODeviceResult PrintTextWithDialog(const std::string& documentName,
                                   const std::string& textContent,
                                   UltraCanvasWindowBase* parent) {
    const NativePrintResult chosen =
        UltraCanvasNativeDialogs::RequestPrintSettings(documentName, parent);
    if (!chosen.IsOK()) {
        return IODeviceResult::Error(IODeviceResultCode::Cancelled,
                                     "The print dialog was cancelled");
    }
    return PrintTextWithSettings(chosen, documentName, textContent);
}

}  // namespace UltraCanvas
