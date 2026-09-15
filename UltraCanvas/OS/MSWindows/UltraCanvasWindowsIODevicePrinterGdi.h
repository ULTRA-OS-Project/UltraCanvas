// OS/MSWindows/UltraCanvasWindowsIODevicePrinterGdi.h
// The two things the spooler backend needs from the GDI renderer: the
// renderer itself, and the call that drives a page source onto a printer DC.
//
// Internal to the Windows backend - the public seam is IPrintRenderer and
// IPrintTransport, and nothing outside OS/MSWindows should include this.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#ifdef _WIN32

#include "../../include/IODeviceManager/UltraCanvasIODevicePrinter.h"

namespace UltraCanvas {
namespace Internal {

// The Native renderer for Windows. Unlike the pass-through NativePrintRenderer
// used with CUPS, this one turns the job into pages to be drawn, because the
// spooler will not process a document on its own.
IPrintRendererPtr CreateWindowsGdiRenderer();

// Opens the printer's device context, runs the document through it
// (StartDoc / StartPage / draw / EndPage / EndDoc) and reports the spooler
// job id. This is the submission half of the GDI path: it does not go through
// StartDocPrinter/WritePrinter at all, because a DC print job is produced by
// the driver from the drawing calls rather than written as bytes.
IODeviceResult PrintPageSourceThroughGdi(const IODeviceInfo& printer,
                                         const IPrintPageSourcePtr& pages,
                                         const IOPrintOptions& options,
                                         const std::string& jobName,
                                         const std::vector<int>& pageRange,
                                         int& outJobId);

}  // namespace Internal
}  // namespace UltraCanvas

#endif  // _WIN32
