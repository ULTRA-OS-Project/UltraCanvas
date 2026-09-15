// core/IODeviceManager/UltraCanvasIODevicePrinter.cpp
// PrinterDevice: renderer selection, option resolution and the render →
// transport path. Platform-neutral; it names no printing API.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "../../include/IODeviceManager/UltraCanvasIODevicePrinter.h"

#include <algorithm>

namespace UltraCanvas {

// ============================================================================
// NATIVE (PASS-THROUGH) RENDERER
// ============================================================================

bool NativePrintRenderer::SupportsPrinter(const IODeviceInfo& printer) const {
    (void)printer;
    // Every printer reachable through a platform queue has a native driver
    // behind it — that is what makes it reachable.
    return true;
}

IODeviceResult NativePrintRenderer::Render(const IOPrintJob& job,
                                           const IOPrinterCapabilities& capabilities,
                                           IOPrintPayload& payload) {
    (void)capabilities;

    if (!job.IsValid()) {
        return IODeviceResult::Error(IODeviceResultCode::InvalidArgument,
                                     "Print job has neither a file nor data");
    }

    // No rendering: the OS driver takes the document as-is. Choosing the
    // native renderer is exactly the choice to let it do that.
    payload.filePath = job.filePath;
    payload.data = job.filePath.empty() ? job.data : std::vector<uint8_t>();
    payload.contentType = job.mimeType;
    payload.isRaw = false;
    return IODeviceResult::Ok();
}

// ============================================================================
// PRINTERDEVICE
// ============================================================================

PrinterDevice::PrinterDevice(const IODeviceInfo& info) : IODevice(info) {}

// A renderer is usable only when what it emits is something the platform
// transport can actually carry. The three checks are symmetric: a
// device-native stream needs a transport that takes raw jobs, a page source
// needs one that can drive a drawing session, and a document needs one whose
// driver will process it.
bool PrinterDevice::RendererIsUsable(const IPrintRendererPtr& renderer,
                                     const IODeviceInfo& info,
                                     const IPrintTransportPtr& transport) const {
    if (!renderer || !renderer->IsAvailable() || !renderer->SupportsPrinter(info)) {
        return false;
    }
    if (!transport) {
        return false;
    }
    if (renderer->ProducesRawStream()) {
        return transport->SupportsRaw();
    }
    if (renderer->ProducesPageSource()) {
        return transport->SupportsPageSource();
    }
    return transport->SupportsDocument();
}

// ===== CAPABILITIES =====

const IOPrinterCapabilities& PrinterDevice::GetCapabilities() {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    if (!capabilitiesLoaded) {
        RefreshCapabilities();
    }
    return capabilities;
}

IODeviceResult PrinterDevice::RefreshCapabilities() {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);

    IOPrinterCapabilities fresh;
    IODeviceResult result = DoGetCapabilities(fresh);
    if (result.success) {
        capabilities = fresh;
        capabilitiesLoaded = true;
    }
    return result;
}

// ===== RENDERER SELECTION =====

void PrinterDevice::AddRenderer(const IPrintRendererPtr& renderer) {
    if (!renderer) {
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);

    // One renderer per kind: a backend adding its own Native renderer
    // replaces the default rather than shadowing it.
    auto existing = std::find_if(renderers.begin(), renderers.end(),
                                 [&](const IPrintRendererPtr& candidate) {
                                     return candidate->GetKind() == renderer->GetKind();
                                 });
    if (existing != renderers.end()) {
        *existing = renderer;
        return;
    }
    renderers.push_back(renderer);
}

std::vector<IOPrintRenderer> PrinterDevice::GetAvailableRenderers() {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);

    const IODeviceInfo info = GetDeviceInfo();
    IPrintTransportPtr transport = GetTransport();

    std::vector<IOPrintRenderer> available;
    for (const auto& renderer : renderers) {
        if (RendererIsUsable(renderer, info, transport)) {
            available.push_back(renderer->GetKind());
        }
    }
    return available;
}

bool PrinterDevice::IsRendererAvailable(IOPrintRenderer renderer) {
    if (renderer == IOPrintRenderer::Auto) {
        return !GetAvailableRenderers().empty();
    }
    const std::vector<IOPrintRenderer> available = GetAvailableRenderers();
    return std::find(available.begin(), available.end(), renderer) != available.end();
}

IODeviceResult PrinterDevice::SetRenderer(IOPrintRenderer renderer) {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);

    if (renderer != IOPrintRenderer::Auto && !IsRendererAvailable(renderer)) {
        // Refused rather than accepted-then-substituted: a caller that asked
        // for GutenPrint needs to know it is not going to get it.
        return IODeviceResult::Error(
            IODeviceResultCode::NotSupported,
            std::string("The ") + IOPrintRendererToString(renderer) +
                " renderer is not available for this printer",
            GetDeviceId());
    }

    options.renderer = renderer;
    return IODeviceResult::Ok(GetDeviceId());
}

IOPrintRenderer PrinterDevice::GetRenderer() {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    IPrintRendererPtr selected = SelectRendererLocked(options.renderer);
    return selected ? selected->GetKind() : IOPrintRenderer::Native;
}

IPrintRendererPtr PrinterDevice::SelectRendererLocked(IOPrintRenderer wanted) {
    const IODeviceInfo info = GetDeviceInfo();
    IPrintTransportPtr transport = GetTransport();

    auto usable = [&](const IPrintRendererPtr& renderer) {
        return RendererIsUsable(renderer, info, transport);
    };

    if (wanted != IOPrintRenderer::Auto) {
        for (const auto& renderer : renderers) {
            if (renderer->GetKind() == wanted && usable(renderer)) {
                return renderer;
            }
        }
        return nullptr;
    }

    // Auto: GutenPrint first where it knows the printer — its parameter set
    // is the richer one — then the platform driver, then driverless IPP.
    for (IOPrintRenderer preference : {IOPrintRenderer::GutenPrint,
                                       IOPrintRenderer::Native,
                                       IOPrintRenderer::IPP}) {
        for (const auto& renderer : renderers) {
            if (renderer->GetKind() == preference && usable(renderer)) {
                return renderer;
            }
        }
    }
    return nullptr;
}

// ===== OPTIONS =====

void PrinterDevice::SetOptions(const IOPrintOptions& newOptions) {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    options = newOptions;
}

IOPrintOptions PrinterDevice::GetOptions() const {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    return options;
}

IOPrintOptions PrinterDevice::ResolveOptions(const IOPrintOptions& requested,
                                             std::vector<std::string>* changes) {
    return ResolvePrintOptions(requested, GetCapabilities(), changes);
}

// ===== PRINTING =====

IODeviceResult PrinterDevice::Print(const IOPrintJob& job) {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);

    IODeviceResult ready = RequireConnected();
    if (!ready.success) {
        return ready;
    }

    if (!job.IsValid()) {
        return IODeviceResult::Error(IODeviceResultCode::InvalidArgument,
                                     "Print job has neither a file nor data",
                                     GetDeviceId());
    }

    IPrintTransportPtr transport = GetTransport();
    if (!transport) {
        return Fail(IODeviceResultCode::BackendUnavailable,
                    "This printer has no transport to submit jobs through");
    }

    // A job asking for Auto defers to the device's own setting, so
    // SetRenderer() means what it says; a job naming a renderer overrides it.
    const IOPrintRenderer wanted = job.options.renderer == IOPrintRenderer::Auto
                                       ? options.renderer
                                       : job.options.renderer;

    IPrintRendererPtr renderer = SelectRendererLocked(wanted);
    if (!renderer) {
        return Fail(IODeviceResultCode::NotSupported,
                    std::string("No usable renderer for this printer (asked for ") +
                        IOPrintRendererToString(wanted) + ")");
    }

    IOPrintJob resolvedJob = job;
    resolvedJob.options = ResolvePrintOptions(job.options, GetCapabilities());
    resolvedJob.options.renderer = renderer->GetKind();
    if (resolvedJob.jobName.empty()) {
        resolvedJob.jobName = resolvedJob.filePath.empty() ? "UltraCanvas document"
                                                           : resolvedJob.filePath;
    }

    IOPrintPayload payload;
    // Set before rendering so a renderer may override it, and after the name
    // has been defaulted above so it is never empty.
    payload.jobName = resolvedJob.jobName;
    IODeviceResult rendered = renderer->Render(resolvedJob, capabilities, payload);
    if (!rendered.success) {
        SetLastError(rendered);
        return rendered;
    }
    if (payload.IsEmpty()) {
        return Fail(IODeviceResultCode::BackendError,
                    std::string("The ") + IOPrintRendererToString(renderer->GetKind()) +
                        " renderer produced nothing to print");
    }

    SetState(IODeviceState::Busy);

    int jobId = 0;
    IODeviceResult submitted =
        transport->Submit(GetDeviceInfo(), payload, resolvedJob.options, jobId);

    SetState(IODeviceState::Ready);

    if (!submitted.success) {
        SetLastError(submitted);
        return submitted;
    }

    IODeviceResult result = IODeviceResult::Ok(GetDeviceId());
    result.backendCode = jobId;
    return result;
}

IODeviceResult PrinterDevice::PrintFile(const std::string& filePath,
                                        const std::string& jobName) {
    IOPrintJob job;
    job.filePath = filePath;
    job.jobName = jobName.empty() ? filePath : jobName;
    job.options = GetOptions();
    return Print(job);
}

IODeviceResult PrinterDevice::CancelJob(int jobId) {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    IODeviceResult ready = RequireConnected();
    if (!ready.success) {
        return ready;
    }
    return DoCancelJob(jobId);
}

IOPrintJobStatus PrinterDevice::GetJobStatus(int jobId) {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    if (!IsConnected()) {
        return IOPrintJobStatus();
    }
    return DoGetJobStatus(jobId);
}

std::vector<IOPrintJobStatus> PrinterDevice::GetJobQueue() {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    if (!IsConnected()) {
        return {};
    }
    return DoGetJobQueue();
}

// ===== STATUS =====

IOPrinterStatus PrinterDevice::GetStatus() {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    if (!IsConnected()) {
        return IOPrinterStatus();
    }
    return DoGetStatus();
}

std::vector<IOSupplyLevel> PrinterDevice::GetSupplyLevels() {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    if (!IsConnected()) {
        return {};
    }
    return DoGetSupplyLevels();
}

bool PrinterDevice::IsReady() {
    return GetStatus().IsReady();
}

}  // namespace UltraCanvas
