- **Driverless printing over IPP, on Linux, macOS and Windows.** An IPP
  Everywhere, AirPrint or Mopria printer now appears as a `PrinterDevice` with
  no driver installed and no print system in between - which on Windows is the
  first route to such a printer at all. One file in `core/IODeviceManager/`
  serves all three platforms, as the eSCL scanner backend does: IPP is HTTP and
  a binary encoding, and nothing in it is platform code.
  - Found over DNS-SD (`_ipp._tcp`, `_ipps._tcp`) through UltraNet's mDNS
    plugin, or named in `ULTRACANVAS_IPP_PRINTERS` for a printer on another
    subnet. Registered as `urn:uuid:<uuid>` and shown by its DNS-SD instance
    name, which is unique where the model name is not.
  - A document the printer renders itself - PDF, JPEG, whatever it lists in
    `document-format-supported` - is sent as it is. Text and other images are
    drawn here and sent as **PWG raster**, which every IPP Everywhere printer
    must accept: landscape pages turned onto the portrait sheet, and every
    second side of a duplex job turned the way the printer's
    `pwg-raster-document-sheet-back` asks, so no even page comes out upside
    down. Copies and page ranges are said once, never twice. Pages are drawn
    inside the printer's own margins, because a PWG raster page is printed
    edge to edge as it is.
  - A printer busy with one job refuses the next; the job waits and asks
    again, as CUPS's IPP backend does, for up to three minutes.
  - Refused by name rather than half-printed: a PDF to a printer that renders
    none, a page range the printer cannot apply, a printer that takes neither
    the document nor PWG raster.
  - Capabilities, status, supplies (`marker-*` and PWG's `printer-supply`),
    job queue, job status and cancel all work. An IPP 1.1 printer is asked
    again in 1.1 and remembered.
  - New: `UltraCanvasIODevicePrinterIPPProtocol.h` (RFC 8010 encoding both
    ways, attribute mapping, the send-or-draw plan) and
    `UltraCanvasIODevicePrinterPwgRaster.h` (PWG 5102.4 writer), both pure;
    `Tests/IODevicePrinterIPPTest` covers them with 232 assertions, the PWG
    compression checked by a decoder written from the specification.
  - `Tests/IODevicePrinterIPPLiveTest` prints to CUPS's reference printer
    `ippeveprinter`, which it starts itself, and is skipped where that is not
    installed. Not yet run against a physical printer.
- **Printing an image crashed a program that had never opened a window.**
  `MakePageSourceForJob`, shared by the GutenPrint, GDI and IPP renderers,
  decoded the image without starting the image library, and the library does
  not fail when it is not started - it crashes. A command-line tool or a
  server printing a PNG hit it; an application that had opened a window did
  not. It now starts the library once, as the eSCL scanner backend already
  did for its own decoding.
- **A printer was going to be listed twice on Linux and macOS, and the design
  that was meant to prevent it had never worked.** The CUPS backend keys a
  queue on `printer-uuid` so the IPP backend could collapse into it - but
  `cupsGetDests2` does not return that option, for CUPS's discovered queues or
  configured ones (checked against CUPS 2.4.7). The IPP backend now matches
  CUPS's queues itself, by the UUID in a `dnssd://` URI, the DNS-SD instance
  name, or the same address, and leaves those printers to CUPS.
