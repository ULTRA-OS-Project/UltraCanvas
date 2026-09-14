// include/IODeviceManager/UltraCanvasIODevicePrinter.h
// PrinterDevice and the renderer/transport seam that lets an application
// choose GutenPrint or the platform's own driver on any platform.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasIODevice.h"
#include "UltraCanvasIODevicePrinterTypes.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// ============================================================================
// PAYLOAD
// ============================================================================

// What a renderer produces and a transport consumes.
struct IOPrintPayload {
    // One of the two. filePath avoids copying a large document through
    // memory when the renderer is only passing it through.
    std::string filePath;
    std::vector<uint8_t> data;

    // MIME type of the above, or empty for a raw stream.
    std::string contentType;

    // True when `data` is the printer's own command stream (ESC/P2, PCL,
    // BJL) and must reach the device untouched. Transports submit these as
    // a raw job: application/vnd.cups-raw under CUPS, datatype "RAW" through
    // the Windows spooler.
    bool isRaw = false;

    bool IsEmpty() const { return filePath.empty() && data.empty(); }
};

// ============================================================================
// RENDERER
// ============================================================================

// Turns a job into a payload. One implementation per IOPrintRenderer value.
class IPrintRenderer {
public:
    virtual ~IPrintRenderer() = default;

    virtual IOPrintRenderer GetKind() const = 0;

    // Whether this build can use the renderer at all: the library or tool
    // it needs is present.
    virtual bool IsAvailable() const = 0;

    // Whether it can drive *this* printer. GutenPrint answers no for a model
    // it does not know, which is why renderer availability is a per-device
    // question and not a per-platform one.
    virtual bool SupportsPrinter(const IODeviceInfo& printer) const = 0;

    // True when Render() emits the printer's own command stream rather than
    // a document the OS driver will process. Such a renderer is only usable
    // where the platform transport can carry a raw job.
    virtual bool ProducesRawStream() const { return false; }

    virtual IODeviceResult Render(const IOPrintJob& job,
                                  const IOPrinterCapabilities& capabilities,
                                  IOPrintPayload& payload) = 0;
};

using IPrintRendererPtr = std::shared_ptr<IPrintRenderer>;

// ============================================================================
// TRANSPORT
// ============================================================================

// Hands a payload to the operating system. Chosen by platform, not by the
// application: CUPS on Linux and macOS, the print spooler on Windows, a
// socket for a queue-less network printer.
class IPrintTransport {
public:
    virtual ~IPrintTransport() = default;

    virtual std::string GetName() const = 0;

    // False when the platform cannot carry a device-native stream, which
    // rules out the GutenPrint renderer on that platform.
    virtual bool SupportsRaw() const = 0;

    // False when the platform cannot take a document and let its own driver
    // process it. CUPS can: it has a filter chain, so a PDF can be handed
    // over as-is. The Windows spooler cannot — it takes device-ready data or
    // spooled EMF/XPS, so a document has to be drawn to a printer DC first,
    // and until that renderer exists the Native renderer is not offered
    // there. Stating it here keeps the gap visible in the API instead of
    // surfacing as a failed job.
    virtual bool SupportsDocument() const { return true; }

    virtual IODeviceResult Submit(const IODeviceInfo& printer,
                                  const IOPrintPayload& payload,
                                  const IOPrintOptions& options,
                                  int& outJobId) = 0;
};

using IPrintTransportPtr = std::shared_ptr<IPrintTransport>;

// ============================================================================
// PASS-THROUGH RENDERER
// ============================================================================

// The Native renderer does no rendering: it hands the document to the OS
// driver, which is the whole point of choosing it. Kept here because every
// platform backend needs exactly this one.
class NativePrintRenderer : public IPrintRenderer {
public:
    IOPrintRenderer GetKind() const override { return IOPrintRenderer::Native; }
    bool IsAvailable() const override { return true; }
    bool SupportsPrinter(const IODeviceInfo& printer) const override;
    IODeviceResult Render(const IOPrintJob& job,
                          const IOPrinterCapabilities& capabilities,
                          IOPrintPayload& payload) override;
};

// ============================================================================
// PRINTERDEVICE
// ============================================================================

class PrinterDevice : public IODevice {
public:
    // ===== CAPABILITIES =====

    // Cached after the first query; Refresh() re-reads from the backend.
    const IOPrinterCapabilities& GetCapabilities();
    IODeviceResult RefreshCapabilities();

    // ===== RENDERER SELECTION =====

    // Which renderers can drive this printer in this build. Answered per
    // device: a renderer is listed only when it is compiled in, its library
    // or tool is present, it recognises this printer, and the platform's
    // transport can carry what it produces.
    std::vector<IOPrintRenderer> GetAvailableRenderers();
    bool IsRendererAvailable(IOPrintRenderer renderer);

    // Fails with NotSupported when the renderer is not in the list above,
    // rather than accepting it and falling back silently at print time.
    IODeviceResult SetRenderer(IOPrintRenderer renderer);

    // The renderer in effect. Never returns Auto: asking for Auto resolves
    // it immediately, so a caller can always show the user what will be used.
    IOPrintRenderer GetRenderer();

    // Registers a renderer for this device. Platform backends add the ones
    // they support; an application can add its own for a proprietary driver.
    void AddRenderer(const IPrintRendererPtr& renderer);

    // ===== OPTIONS =====

    void SetOptions(const IOPrintOptions& options);
    IOPrintOptions GetOptions() const;

    // What SetOptions/Print would actually do with these, after clamping to
    // this printer's capabilities. Lets a print dialog show the user the
    // substitutions before the job is sent rather than after.
    IOPrintOptions ResolveOptions(const IOPrintOptions& requested,
                                  std::vector<std::string>* changes = nullptr);

    // ===== PRINTING =====

    // Renders with the selected renderer and submits through the platform
    // transport. On success the result's backendCode carries the job id.
    IODeviceResult Print(const IOPrintJob& job);
    IODeviceResult PrintFile(const std::string& filePath,
                             const std::string& jobName = std::string());

    IODeviceResult CancelJob(int jobId);
    IOPrintJobStatus GetJobStatus(int jobId);
    std::vector<IOPrintJobStatus> GetJobQueue();

    // ===== STATUS =====

    IOPrinterStatus GetStatus();
    std::vector<IOSupplyLevel> GetSupplyLevels();
    bool IsReady();

protected:
    explicit PrinterDevice(const IODeviceInfo& info);

    // ===== BACKEND HOOKS =====

    virtual IODeviceResult DoGetCapabilities(IOPrinterCapabilities& capabilities) = 0;
    virtual IOPrinterStatus DoGetStatus() = 0;
    virtual std::vector<IOSupplyLevel> DoGetSupplyLevels() = 0;
    virtual IODeviceResult DoCancelJob(int jobId) = 0;
    virtual IOPrintJobStatus DoGetJobStatus(int jobId) = 0;
    virtual std::vector<IOPrintJobStatus> DoGetJobQueue() = 0;

    // The platform transport. Never null for a usable device.
    virtual IPrintTransportPtr GetTransport() = 0;

private:
    // Resolves Auto, or verifies an explicit choice. Caller must hold
    // deviceMutex.
    IPrintRendererPtr SelectRendererLocked(IOPrintRenderer wanted);

    // Whether what this renderer emits is something the transport can carry.
    bool RendererIsUsable(const IPrintRendererPtr& renderer,
                          const IODeviceInfo& info,
                          const IPrintTransportPtr& transport) const;

    std::vector<IPrintRendererPtr> renderers;
    IOPrintOptions options;
    IOPrinterCapabilities capabilities;
    bool capabilitiesLoaded = false;
};

using PrinterDevicePtr = std::shared_ptr<PrinterDevice>;

} // namespace UltraCanvas
