// include/IODeviceManager/UltraCanvasIODevicePrinterGutenPrint.h
// The GutenPrint renderer: GutenPrint's own tools, run as programs.
//
// **libgutenprint is not linked, and that is the point.** It is
// GPL-2.0-or-later and UltraCanvas is MIT, so linking it would make every
// distributed binary GPL. This repository already has a pattern for GPL tools
// it uses but does not become - QEMU and Wine are run, not linked - and
// GutenPrint fits it exactly, because GutenPrint ships its own programs:
//
//   gutenprint.5.3          the CUPS driver interface. `list` names every
//                           model it supports, with each one's IEEE-1284
//                           device id; `cat <uri>` writes that model's PPD.
//   rastertogutenprint.5.3  the CUPS filter. Reads a page of CUPS raster on
//                           its standard input and writes the printer's own
//                           command language on its standard output.
//
// So the renderer rasterises a page, pipes it through the filter, and the
// bytes that come back are the payload. They are a device-native stream, so
// they go out as a raw job - which both raw transports already carry, on
// Linux, macOS and Windows alike. Nothing about the transport changes.
//
// The licence effect of running a program is none: no GutenPrint code is
// linked into, distributed with, or derived from anything here.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasIODevicePrinter.h"

#include <string>
#include <vector>

namespace UltraCanvas {

// ============================================================================
// FINDING THE TOOLS
// ============================================================================

struct IOGutenPrintTools {
    std::string driver;   // gutenprint.5.3
    std::string filter;   // rastertogutenprint.5.3

    bool IsComplete() const { return !driver.empty() && !filter.empty(); }
};

// Looks for both tools and reports what it found.
//
// ULTRACANVAS_GUTENPRINT_DRIVER and ULTRACANVAS_GUTENPRINT_FILTER name them
// outright when set, which is how a packaged build points at binaries shipped
// beside the application - the arrangement Windows needs, since nothing
// installs GutenPrint there by default.
//
// Finding nothing is not an error. It means this machine has no GutenPrint,
// the renderer is not offered, and Native is used instead.
IOGutenPrintTools FindGutenPrintTools();

// ============================================================================
// MODELS
// ============================================================================

// One printer model GutenPrint supports, as `gutenprint.5.3 list` reports it.
struct IOGutenPrintModel {
    std::string uri;            // "gutenprint.5.3://escp2-r2400/expert"
    std::string manufacturer;   // "Epson"
    std::string description;    // "Epson Stylus Photo R2400 - CUPS+Gutenprint v5.3.4"
    std::string deviceId;       // "MFG:EPSON;MDL:Stylus Photo R2400;..." - may be empty
};

// Parses the listing. Each line is five fields:
//
//   "<uri>" <language> "<manufacturer>" "<description>" "<device id>"
//
// A line that does not parse is skipped rather than failing the lot: the
// listing is three and a half thousand lines from a tool that may be a
// different version than this code was written against, and one odd entry
// should cost one model, not all of them.
std::vector<IOGutenPrintModel> ParseGutenPrintModels(const std::string& listing);

// The model driving this printer, or an empty string.
//
// Matched on what the printer calls itself rather than on the queue name,
// which is whatever the user typed. The IEEE-1284 device id is tried first
// because it is the printer's own answer to "what are you"; the human
// description is the fallback, since GutenPrint leaves the device id empty
// for the older models.
std::string MatchGutenPrintModel(const std::vector<IOGutenPrintModel>& models,
                                 const std::string& manufacturer,
                                 const std::string& model);

// ============================================================================
// THE RENDERER
// ============================================================================

// Null when this build has no way to run the tools.
IPrintRendererPtr CreateGutenPrintRenderer();

}  // namespace UltraCanvas
