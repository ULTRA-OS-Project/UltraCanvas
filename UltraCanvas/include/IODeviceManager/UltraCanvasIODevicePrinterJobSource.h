// include/IODeviceManager/UltraCanvasIODevicePrinterJobSource.h
// Turning a print job into pages to be drawn.
//
// Two renderers need exactly this and no more: the Windows GDI renderer,
// which draws those pages onto a printer device context, and the GutenPrint
// renderer, which draws them onto a bitmap for GutenPrint's filter. What a
// job contains, and how its type is worked out, has nothing to do with either
// destination - so it is decided once, here, rather than twice in two files
// that would drift.
//
// Kept out of UltraCanvasIODevicePrinterPage.h, where the page sources
// themselves live, because decoding an image pulls in the imaging stack and
// that header is linked by the printer tests, which need no such thing.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasIODevicePrinterPage.h"
#include "UltraCanvasIODevicePrinterTypes.h"

#include <string>

namespace UltraCanvas {

// Builds the page source for a job, and reports the payload content type that
// goes with it.
//
// Fails, by name, for a document this cannot paginate - a PDF needs a
// renderer that can lay one out, and half-printing it would be worse than
// saying so.
IODeviceResult MakePageSourceForJob(const IOPrintJob& job,
                                    IPrintPageSourcePtr& outPages,
                                    std::string& outContentType);

}  // namespace UltraCanvas
