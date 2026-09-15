// include/IODeviceManager/UltraCanvasIODevicePrintDialog.h
// The join between the OS print dialog and PrinterDevice: what the user
// picked, actually printed with.
//
// Why this exists. UltraCanvasNativeDialogs::ShowPrintDialog() has shipped for
// a while and Texter and UltraFiler both use it, but each platform printed by
// itself and discarded everything the dialog collected. Linux built a real GTK
// print dialog, read the page setup and the settings, used neither, and handed
// the text to `lpr`; Windows did not show a print dialog at all, it wrote a
// temp file and asked the shell to print it. Copies, collation, paper size,
// orientation, duplex and page range were all chosen by the user and then
// dropped on the floor.
//
// PrinterDevice already honours every one of those. So the dialog's job is
// narrowed to *asking* - that is RequestPrintSettings(), which returns a
// NativePrintResult - and the answer is carried to a printer here, once,
// for every platform.
//
// The split also puts the interesting part where it can be tested: matching
// the name a dialog returns to a device, and turning a dialog answer into an
// IOPrintJob, are both ordinary functions over data, and neither needs a
// dialog or a printer to run.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasIODevicePrinter.h"

#include <string>
#include <vector>

namespace UltraCanvas {

// Only ever a parent pointer here, so the window stack is forward-declared
// rather than included: pulling UltraCanvasNativeDialogs.h in would bring
// UltraCanvasModalDialog.h and with it every widget header, into a module
// that depends on no UI at all.
class UltraCanvasWindowBase;

// ============================================================================
// MATCHING A DIALOG'S ANSWER TO A DEVICE
// ============================================================================

// Which of `devices` is the printer a dialog named, or null.
//
// The name a print dialog returns is the queue name - GTK gives the CUPS
// destination, Windows gives the spooler's printer name - and both backends
// put exactly that in IODeviceInfo::connectionPath, so that is the field
// matched first. A match on the display name is accepted next, because a
// backend added later may only be able to offer one name, and a
// case-insensitive pass runs last: a Windows printer name is not
// case-sensitive, and a user's own configuration is not a good place to be
// strict about it.
//
// Devices that are not printers are ignored rather than rejected, so this can
// be handed the registry's whole device list.
PrinterDevicePtr MatchPrinterByName(const std::vector<IODevicePtr>& devices,
                                    const std::string& printerName);

// ============================================================================
// TURNING A DIALOG'S ANSWER INTO A JOB
// ============================================================================

// Builds the job a dialog answer describes, for a plain-text document.
//
// Nothing is defaulted here beyond the document name: the options come from
// the dialog because the user set them there, and PrinterDevice::Print()
// resolves them against the printer's capabilities afterwards, reporting any
// substitution. Two layers second-guessing the user is how a print dialog
// stops meaning anything.
IOPrintJob MakeTextPrintJob(const IOPrintDialogChoice& chosen,
                            const std::string& documentName,
                            const std::string& textContent);

// ============================================================================
// PRINTING
// ============================================================================

// Submits a job built from a dialog answer, to the printer that answer names.
//
// Enumerates printers if the registry has none yet, connects the device if it
// is not already connected - and disconnects it again only if this call was
// what connected it, so a caller holding an open session does not have it
// closed underneath them.
//
// On success the result's backendCode carries the spooler's job id.
IODeviceResult PrintTextWithSettings(const IOPrintDialogChoice& chosen,
                                     const std::string& documentName,
                                     const std::string& textContent);

// Shows the OS print dialog and prints the text with what the user chose.
//
// This is what UltraCanvasNativeDialogs::ShowPrintDialog() now does; the
// difference is the return type. A cancelled dialog comes back as a
// Cancelled result rather than as a failure, so a caller can tell "the user
// changed their mind" from "the printer was not there" - which the bool form
// cannot, and which is the difference between showing an error and showing
// nothing.
IODeviceResult PrintTextWithDialog(const std::string& documentName,
                                   const std::string& textContent,
                                   UltraCanvasWindowBase* parent = nullptr);

}  // namespace UltraCanvas
