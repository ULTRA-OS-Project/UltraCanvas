// core/IODeviceManager/UltraCanvasIODevicePrintDialog.cpp
// The half of the print-dialog bridge that is ordinary functions over data:
// matching the printer name a dialog returns to a device, and turning a
// dialog answer into a print job.
//
// Split from the submitting half (UltraCanvasIODevicePrintDialogSubmit.cpp)
// so it can be tested. Referencing UltraCanvasNativeDialogs::
// RequestPrintSettings() would put a platform dialog implementation in the
// link line of anything that used any of this, and the printer tests
// deliberately link the platform-neutral printer sources and nothing else -
// no UI, no registry, no hardware. Neither function below needs any of that,
// so neither is on the far side of that line.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "../../include/IODeviceManager/UltraCanvasIODevicePrintDialog.h"

#include <algorithm>
#include <cctype>

namespace UltraCanvas {

namespace {

std::string LowerAscii(const std::string& text) {
    std::string lowered = text;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return lowered;
}

}  // namespace

// ============================================================================
// MATCHING
// ============================================================================

PrinterDevicePtr MatchPrinterByName(const std::vector<IODevicePtr>& devices,
                                    const std::string& printerName) {
    if (printerName.empty()) {
        return nullptr;
    }

    // Four passes rather than one loop with a scoring function: the order the
    // fields are tried in *is* the rule, and a score would bury it. An exact
    // connectionPath match must beat a case-insensitive display-name match on
    // a different device, which is precisely what two printers named "Office"
    // and "office" on one machine would otherwise turn into a coin toss.
    const std::string wanted = LowerAscii(printerName);

    PrinterDevicePtr namedExactly;
    PrinterDevicePtr pathFolded;
    PrinterDevicePtr nameFolded;

    for (const IODevicePtr& device : devices) {
        if (!device || device->GetCategory() != IODeviceCategory::Printer) {
            continue;
        }
        PrinterDevicePtr printer = std::dynamic_pointer_cast<PrinterDevice>(device);
        if (!printer) {
            continue;
        }

        const IODeviceInfo info = printer->GetDeviceInfo();
        if (info.connectionPath == printerName) {
            return printer;     // the queue name, which is what a dialog gives
        }
        if (!namedExactly && info.name == printerName) {
            namedExactly = printer;
        }
        if (!pathFolded && LowerAscii(info.connectionPath) == wanted) {
            pathFolded = printer;
        }
        if (!nameFolded && LowerAscii(info.name) == wanted) {
            nameFolded = printer;
        }
    }

    if (namedExactly) return namedExactly;
    if (pathFolded)   return pathFolded;
    return nameFolded;
}

// ============================================================================
// JOB CONSTRUCTION
// ============================================================================

IOPrintJob MakeTextPrintJob(const IOPrintDialogChoice& chosen,
                            const std::string& documentName,
                            const std::string& textContent) {
    IOPrintJob job;
    job.jobName = documentName.empty() ? std::string("UltraCanvas document")
                                       : documentName;
    job.data.assign(textContent.begin(), textContent.end());

    // Stated rather than inferred. Both renderers that can take this job work
    // out what a payload is from the MIME type first and the file extension
    // second, and there is no file here for the second to read - so leaving
    // it empty is how an in-memory document gets refused for saying nothing
    // about its type.
    job.mimeType = "text/plain";

    job.options = chosen.options;
    job.pageRange = chosen.pageRange;
    return job;
}

}  // namespace UltraCanvas
