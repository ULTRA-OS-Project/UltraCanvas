// OS/MSWindows/UltraCanvasWindowsIODevicePrinter.cpp
// Windows print spooler backend: enumeration, capabilities, status, the job
// queue, and the transport that carries a device-native stream as a RAW job.
//
// The RAW path is what lets the GutenPrint renderer reach a printer on
// Windows: libgutenprint emits the printer's own command stream, and
// StartDocPrinter with datatype "RAW" hands it to the device untouched. No
// CUPS is involved, which is the whole point - only GutenPrint's *CUPS
// driver* is Unix-only, not the library.
//
// Supply levels come from the driver's bidirectional channel (IBidiSpl), which
// the spooler itself does not expose. When the driver says nothing and the
// queue prints to a network address, the printer behind it is asked directly
// over IPP (builds with UltraNet only).
// Version: 0.3.0
// Author: UltraCanvas Framework / ULTRA OS

#ifdef _WIN32

#include "UltraCanvasWindowsIODevicePrinterGdi.h"

#include "../../include/IODeviceManager/UltraCanvasIODevicePrinter.h"
#include "../../include/IODeviceManager/UltraCanvasIODevicePrinterGutenPrint.h"
#include "../../include/IODeviceManager/UltraCanvasIODeviceManager.h"
#include "../../include/IODeviceManager/UltraCanvasIODevicePrinterIPP.h"
#include "../../include/IODeviceManager/UltraCanvasIODevicePrinterIPPProtocol.h"
#include "../../include/UltraCanvasUtils.h"

#include <windows.h>
#include <winspool.h>
#include <objbase.h>
#include <bidispl.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace UltraCanvas {
namespace {

// ============================================================================
// SMALL HELPERS
// ============================================================================

std::string LastErrorText(DWORD error) {
    LPWSTR buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

    std::string message;
    if (length && buffer) {
        message = WideToUtf8(std::wstring(buffer, length));
        while (!message.empty() &&
               (message.back() == '\n' || message.back() == '\r')) {
            message.pop_back();
        }
    }
    if (buffer) {
        LocalFree(buffer);
    }
    if (message.empty()) {
        message = "Windows error " + std::to_string(error);
    }
    return message;
}

std::string FromWide(const wchar_t* text) {
    return text ? WideToUtf8(std::wstring(text)) : std::string();
}

// RAII for a spooler handle; OpenPrinterW/ClosePrinter must always pair even
// on the error paths below.
class PrinterHandle {
public:
    explicit PrinterHandle(const std::wstring& name) {
        // The spooler takes a non-const name even though it does not write
        // to it.
        std::wstring mutableName = name;
        if (!OpenPrinterW(mutableName.data(), &handle, nullptr)) {
            handle = nullptr;
        }
    }

    ~PrinterHandle() {
        if (handle) {
            ClosePrinter(handle);
        }
    }

    PrinterHandle(const PrinterHandle&) = delete;
    PrinterHandle& operator=(const PrinterHandle&) = delete;

    bool IsOpen() const { return handle != nullptr; }
    HANDLE Get() const { return handle; }

private:
    HANDLE handle = nullptr;
};

// GetPrinterW is a two-call API: ask for the size, then fill the buffer.
std::vector<uint8_t> QueryPrinterInfo(HANDLE printer, DWORD level) {
    DWORD needed = 0;
    GetPrinterW(printer, level, nullptr, 0, &needed);
    if (needed == 0) {
        return {};
    }
    std::vector<uint8_t> buffer(needed);
    if (!GetPrinterW(printer, level, buffer.data(), needed, &needed)) {
        return {};
    }
    return buffer;
}

// ============================================================================
// SUPPLY LEVELS (BIDI)
// ============================================================================

// The bidi class and interface IDs, spelled out: bidispl.h declares them, but
// not every toolchain's uuid library defines them, and naming CLSID_BidiSpl
// would then compile and fail to link. Values as in the Windows SDK.
const CLSID kClsidBidiSpl = {0x2a614240, 0xa4c5, 0x4c33,
                             {0xbd, 0x87, 0x1b, 0xc7, 0x09, 0x33, 0x16, 0x39}};
const CLSID kClsidBidiRequest = {0xb9162a23, 0x45f9, 0x47cc,
                                 {0x80, 0xf5, 0xfe, 0x0f, 0xe9, 0xb9, 0xe1, 0xa2}};
const IID kIidBidiSpl = {0xd580dc0e, 0xde39, 0x4649,
                         {0xba, 0xa8, 0xbf, 0x0b, 0x85, 0xa0, 0x3a, 0x97}};
const IID kIidBidiRequest = {0x8f348bd7, 0x4b47, 0x4755,
                             {0x8a, 0x9d, 0x0f, 0x42, 0x2d, 0xf3, 0xdc, 0x89}};

// Releases a COM interface on every path out of the query below.
template <typename T>
class ComRef {
public:
    ComRef() = default;
    ~ComRef() {
        if (ptr) {
            ptr->Release();
        }
    }
    ComRef(const ComRef&) = delete;
    ComRef& operator=(const ComRef&) = delete;

    T* operator->() const { return ptr; }
    T* Get() const { return ptr; }
    void** Out() { return reinterpret_cast<void**>(&ptr); }

private:
    T* ptr = nullptr;
};

// COM for the duration of one query. The caller may be a worker thread that
// has never touched COM, or the UI thread, which is already OLE-initialized
// as an STA: that answers RPC_E_CHANGED_MODE, where COM is usable but not
// ours to tear down.
class ComScope {
public:
    ComScope() : hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComScope() {
        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

    bool Usable() const { return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE; }

private:
    HRESULT hr;
};

// A string-typed bidi value is UTF-16 with a size in bytes that may or may
// not count the terminator.
std::string BidiText(const BYTE* data, ULONG size) {
    if (!data || size < sizeof(wchar_t)) {
        return {};
    }
    std::wstring text(reinterpret_cast<const wchar_t*>(data), size / sizeof(wchar_t));
    while (!text.empty() && text.back() == L'\0') {
        text.pop_back();
    }
    return WideToUtf8(text);
}

// Asks the printer's driver for every \Printer.Consumables value. A driver
// with no bidi support, a printer that is off, or a port that cannot talk
// back all end the same way - an empty list, which reads as "not reported".
std::vector<IOSupplyLevel> QueryBidiSupplyLevels(const std::wstring& printerName) {
    ComScope com;
    if (!com.Usable()) {
        return {};
    }

    ComRef<IBidiSpl> bidi;
    if (FAILED(CoCreateInstance(kClsidBidiSpl, nullptr, CLSCTX_INPROC_SERVER,
                                kIidBidiSpl, bidi.Out()))) {
        return {};
    }
    if (FAILED(bidi->BindDevice(printerName.c_str(), BIDI_ACCESS_USER))) {
        return {};
    }

    std::vector<IOBidiConsumableValue> values;
    {
        ComRef<IBidiRequest> request;
        if (SUCCEEDED(CoCreateInstance(kClsidBidiRequest, nullptr, CLSCTX_INPROC_SERVER,
                                       kIidBidiRequest, request.Out())) &&
            SUCCEEDED(request->SetSchema(L"\\Printer.Consumables")) &&
            SUCCEEDED(bidi->SendRecv(BIDI_ACTION_GET_ALL, request.Get()))) {
            HRESULT answer = E_FAIL;
            DWORD count = 0;
            if (SUCCEEDED(request->GetResult(&answer)) && SUCCEEDED(answer) &&
                SUCCEEDED(request->GetEnumCount(&count))) {
                for (DWORD i = 0; i < count; ++i) {
                    LPWSTR schema = nullptr;
                    DWORD type = BIDI_NULL;
                    BYTE* data = nullptr;
                    ULONG size = 0;
                    if (FAILED(request->GetOutputData(i, &schema, &type, &data, &size))) {
                        continue;
                    }
                    IOBidiConsumableValue value;
                    value.schema = FromWide(schema);
                    if (type == BIDI_INT && data && size >= sizeof(int32_t)) {
                        int32_t number = 0;
                        std::memcpy(&number, data, sizeof(number));
                        value.number = number;
                        value.isNumber = true;
                    } else if (type == BIDI_STRING || type == BIDI_TEXT ||
                               type == BIDI_ENUM) {
                        value.text = BidiText(data, size);
                    }
                    CoTaskMemFree(schema);
                    CoTaskMemFree(data);
                    values.push_back(std::move(value));
                }
            }
        }
    }

    bidi->UnbindDevice();
    return IOSupplyLevelsFromBidi(values);
}

// ============================================================================
// SUPPLY LEVELS (IPP, WHEN THE DRIVER KEEPS THEM)
// ============================================================================

#if defined(ULTRACANVAS_HAS_NET)

// The address a Standard TCP/IP port is configured with, asked of its port
// monitor. Empty for any other kind of port, or when the monitor will not
// tell this user; the port's name is then all there is to go on.
std::string TcpPortHostAddress(const std::wstring& port) {
    std::wstring xcvName = L",XcvPort " + port;
    PRINTER_DEFAULTSW defaults{nullptr, nullptr, SERVER_ACCESS_ENUMERATE};
    HANDLE xcv = nullptr;
    if (!OpenPrinterW(xcvName.data(), &xcv, &defaults)) {
        return {};
    }
    std::string host;
    for (const wchar_t* query : {L"HostAddress", L"IPAddress"}) {
        wchar_t buffer[256] = {};
        DWORD needed = 0;
        DWORD status = ERROR_INVALID_FUNCTION;
        if (XcvDataW(xcv, query, nullptr, 0, reinterpret_cast<PBYTE>(buffer),
                     static_cast<DWORD>(sizeof(buffer) - sizeof(wchar_t)), &needed,
                     &status) &&
            status == ERROR_SUCCESS) {
            host = FromWide(buffer);
            if (!host.empty()) {
                break;
            }
        }
    }
    ClosePrinter(xcv);
    return host;
}

// The IPP addresses a queue's printer might answer at, from the ports it
// prints to - several, comma-separated, for a pooled queue.
std::vector<std::string> IppUrisForQueue(const std::wstring& printerName) {
    PrinterHandle handle(printerName);
    if (!handle.IsOpen()) {
        return {};
    }
    std::vector<uint8_t> buffer = QueryPrinterInfo(handle.Get(), 2);
    if (buffer.empty()) {
        return {};
    }
    const PRINTER_INFO_2W* info = reinterpret_cast<const PRINTER_INFO_2W*>(buffer.data());
    const std::wstring ports = info->pPortName ? info->pPortName : L"";

    std::vector<std::string> uris;
    size_t at = 0;
    while (at <= ports.size()) {
        size_t end = ports.find(L',', at);
        if (end == std::wstring::npos) {
            end = ports.size();
        }
        std::wstring port = ports.substr(at, end - at);
        while (!port.empty() && port.front() == L' ') {
            port.erase(0, 1);
        }
        if (!port.empty()) {
            for (const std::string& uri :
                 IppUrisForWindowsPort(WideToUtf8(port), TcpPortHostAddress(port))) {
                if (std::find(uris.begin(), uris.end(), uri) == uris.end()) {
                    uris.push_back(uri);
                }
            }
        }
        at = end + 1;
    }
    return uris;
}

#endif  // ULTRACANVAS_HAS_NET

// ============================================================================
// TRANSPORT
// ============================================================================

class WindowsPrintTransport : public IPrintTransport {
public:
    std::string GetName() const override { return "Windows Spooler"; }

    // A device-native stream goes through as datatype "RAW", which is
    // exactly what a GutenPrint-rendered page needs.
    bool SupportsRaw() const override { return true; }

    // But the spooler will not take a PDF or a PNG and work out what to do
    // with it the way CUPS's filter chain does: it wants device-ready data.
    // This stays false, and it is not a gap - it is what Windows is.
    bool SupportsDocument() const override { return false; }

    // The way a document does reach a Windows printer is by being drawn onto
    // a device context, which the GDI renderer produces pages for. That is
    // what makes the Native renderer available here.
    bool SupportsPageSource() const override { return true; }

    IODeviceResult Submit(const IODeviceInfo& printer,
                          const IOPrintPayload& payload,
                          const IOPrintOptions& options,
                          int& outJobId) override {
        // A page source is not submitted as bytes at all: the driver builds
        // the job from the drawing calls, so this hands straight over to the
        // GDI half rather than opening a spooler handle here.
        if (payload.pages) {
            return Internal::PrintPageSourceThroughGdi(
                printer, payload.pages, options, payload.jobName,
                payload.pageRange, outJobId);
        }

        (void)options;

        if (!payload.isRaw) {
            return IODeviceResult::Error(
                IODeviceResultCode::NotSupported,
                "The Windows spooler takes device-ready data; a document has "
                "to come from a renderer that produces pages to draw",
                printer.deviceId);
        }

        const std::wstring name = Utf8ToWide(printer.connectionPath);
        PrinterHandle handle(name);
        if (!handle.IsOpen()) {
            const DWORD error = GetLastError();
            return IODeviceResult::BackendError(
                IODeviceResultCode::DeviceNotFound,
                "Could not open printer '" + printer.connectionPath + "': " +
                    LastErrorText(error),
                static_cast<int>(error), printer.deviceId);
        }

        std::wstring jobName = Utf8ToWide(
            payload.jobName.empty() ? std::string("UltraCanvas document")
                                    : payload.jobName);
        std::wstring dataType = L"RAW";

        DOC_INFO_1W docInfo = {};
        docInfo.pDocName = jobName.data();
        docInfo.pOutputFile = nullptr;
        docInfo.pDatatype = dataType.data();

        const DWORD jobId = StartDocPrinterW(handle.Get(), 1,
                                             reinterpret_cast<LPBYTE>(&docInfo));
        if (jobId == 0) {
            const DWORD error = GetLastError();
            return IODeviceResult::BackendError(
                IODeviceResultCode::BackendError,
                "The spooler refused the job: " + LastErrorText(error),
                static_cast<int>(error), printer.deviceId);
        }

        IODeviceResult result = WritePages(handle.Get(), payload, printer);

        EndDocPrinter(handle.Get());

        if (result.success) {
            outJobId = static_cast<int>(jobId);
        }
        return result;
    }

private:
    IODeviceResult WritePages(HANDLE printer, const IOPrintPayload& payload,
                              const IODeviceInfo& info) {
        if (!StartPagePrinter(printer)) {
            const DWORD error = GetLastError();
            return IODeviceResult::BackendError(
                IODeviceResultCode::BackendError,
                "The spooler would not start the page: " + LastErrorText(error),
                static_cast<int>(error), info.deviceId);
        }

        const bool written = payload.filePath.empty()
                                 ? WriteBuffer(printer, payload.data)
                                 : WriteFile(printer, payload.filePath);
        const DWORD writeError = written ? 0 : GetLastError();

        EndPagePrinter(printer);

        if (!written) {
            return IODeviceResult::BackendError(
                IODeviceResultCode::IOError,
                "Could not send the print data: " + LastErrorText(writeError),
                static_cast<int>(writeError), info.deviceId);
        }
        return IODeviceResult::Ok(info.deviceId);
    }

    static bool WriteBuffer(HANDLE printer, const std::vector<uint8_t>& data) {
        if (data.empty()) {
            return true;
        }
        DWORD written = 0;
        if (!WritePrinter(printer, const_cast<uint8_t*>(data.data()),
                          static_cast<DWORD>(data.size()), &written)) {
            return false;
        }
        return written == data.size();
    }

    // Streamed rather than slurped: a print-ready raster of a photo page can
    // be hundreds of megabytes.
    static bool WriteFile(HANDLE printer, const std::string& path) {
        FILE* file = nullptr;
        if (_wfopen_s(&file, Utf8ToWide(path).c_str(), L"rb") != 0 || !file) {
            SetLastError(ERROR_FILE_NOT_FOUND);
            return false;
        }

        std::vector<uint8_t> buffer(64 * 1024);
        bool ok = true;
        size_t read = 0;
        while ((read = std::fread(buffer.data(), 1, buffer.size(), file)) > 0) {
            DWORD written = 0;
            if (!WritePrinter(printer, buffer.data(), static_cast<DWORD>(read),
                              &written) ||
                written != read) {
                ok = false;
                break;
            }
        }
        std::fclose(file);
        return ok;
    }
};

IPrintTransportPtr SharedWindowsTransport() {
    static IPrintTransportPtr transport = std::make_shared<WindowsPrintTransport>();
    return transport;
}

// ============================================================================
// DEVICE
// ============================================================================

class WindowsPrinterDevice : public PrinterDevice {
public:
    explicit WindowsPrinterDevice(const IODeviceInfo& info) : PrinterDevice(info) {
        // Not the pass-through NativePrintRenderer that CUPS uses: the
        // spooler cannot process a document on its own, so Native on Windows
        // means drawing onto a printer DC. Same IOPrintRenderer::Native to
        // the caller, different implementation underneath - which is the
        // point of choosing a renderer by kind rather than by class.
        AddRenderer(Internal::CreateWindowsGdiRenderer());
        // GutenPrint is offered alongside, and answers for itself whether it
        // knows this model. It emits the printer's own command language, so
        // it needs a transport that carries a raw job - which this one does.
        AddRenderer(CreateGutenPrintRenderer());
    }

protected:
    IODeviceResult DoConnect() override {
        const std::wstring name = Utf8ToWide(GetDeviceInfo().connectionPath);
        PrinterHandle handle(name);
        if (!handle.IsOpen()) {
            const DWORD error = GetLastError();
            return IODeviceResult::BackendError(
                IODeviceResultCode::DeviceNotFound,
                "Could not open printer '" + GetDeviceInfo().connectionPath +
                    "': " + LastErrorText(error),
                static_cast<int>(error));
        }
        // Nothing is cached across the session: the spooler handle is cheap
        // to reopen, and holding one open across a suspend or a driver
        // upgrade is how a printer ends up unusable until the app restarts.
        return IODeviceResult::Ok();
    }

    void DoDisconnect() override {}

    IODeviceResult DoGetCapabilities(IOPrinterCapabilities& capabilities) override {
        const std::wstring name = Utf8ToWide(GetDeviceInfo().connectionPath);

        const int paperCount = DeviceCapabilitiesW(name.c_str(), nullptr,
                                                   DC_PAPERSIZE, nullptr, nullptr);
        if (paperCount > 0) {
            // DC_PAPERSIZE is in tenths of a millimetre; IOPaperDimensions is
            // in hundredths, so each measure is scaled rather than renamed.
            std::vector<POINT> sizes(static_cast<size_t>(paperCount));
            if (DeviceCapabilitiesW(name.c_str(), nullptr, DC_PAPERSIZE,
                                    reinterpret_cast<LPWSTR>(sizes.data()),
                                    nullptr) == paperCount) {
                for (const POINT& size : sizes) {
                    const IOPaperSize paper =
                        PaperSizeFromTenthsMM(size.x, size.y);
                    if (paper != IOPaperSize::Unknown) {
                        AddUnique(capabilities.paperSizes, paper);
                    }
                    const int widthHundredths = static_cast<int>(size.x) * 10;
                    const int heightHundredths = static_cast<int>(size.y) * 10;
                    if (widthHundredths > capabilities.maxCustomSize.widthHundredthsMM) {
                        capabilities.maxCustomSize.widthHundredthsMM = widthHundredths;
                    }
                    if (heightHundredths > capabilities.maxCustomSize.heightHundredthsMM) {
                        capabilities.maxCustomSize.heightHundredthsMM = heightHundredths;
                    }
                }
            }
        }

        capabilities.supportsDuplex = IOSupportFrom(
            DeviceCapabilitiesW(name.c_str(), nullptr, DC_DUPLEX, nullptr, nullptr) == 1);

        capabilities.supportsColor = IOSupportFrom(
            DeviceCapabilitiesW(name.c_str(), nullptr, DC_COLORDEVICE, nullptr,
                                nullptr) == 1);

        capabilities.supportsCollate = IOSupportFrom(
            DeviceCapabilitiesW(name.c_str(), nullptr, DC_COLLATE, nullptr, nullptr) == 1);

        const int maxCopies =
            DeviceCapabilitiesW(name.c_str(), nullptr, DC_COPIES, nullptr, nullptr);
        capabilities.maxCopies = maxCopies > 0 ? maxCopies : 0;

        capabilities.qualities = {IOPrintQuality::Draft, IOPrintQuality::Normal,
                                  IOPrintQuality::High};

        // Media types and inksets are left empty: the spooler has no
        // vocabulary for them, and an empty list reads as "not reported"
        // rather than "nothing supported".
        return IODeviceResult::Ok(GetDeviceId());
    }

    IOPrinterStatus DoGetStatus() override {
        IOPrinterStatus status;

        PrinterHandle handle(Utf8ToWide(GetDeviceInfo().connectionPath));
        if (!handle.IsOpen()) {
            return status;
        }

        std::vector<uint8_t> buffer = QueryPrinterInfo(handle.Get(), 2);
        if (buffer.empty()) {
            return status;
        }

        const PRINTER_INFO_2W* info =
            reinterpret_cast<const PRINTER_INFO_2W*>(buffer.data());

        status.jobsQueued = static_cast<int>(info->cJobs);
        status.acceptingJobs = (info->Status & PRINTER_STATUS_NOT_AVAILABLE) == 0 &&
                               (info->Status & PRINTER_STATUS_OFFLINE) == 0;

        if (info->Status == 0) {
            status.state = IOPrinterState::Idle;
        } else if (info->Status & (PRINTER_STATUS_PRINTING |
                                   PRINTER_STATUS_PROCESSING |
                                   PRINTER_STATUS_BUSY | PRINTER_STATUS_WARMING_UP)) {
            status.state = IOPrinterState::Printing;
        } else if (info->Status & (PRINTER_STATUS_ERROR | PRINTER_STATUS_PAPER_JAM |
                                   PRINTER_STATUS_PAPER_OUT | PRINTER_STATUS_OFFLINE |
                                   PRINTER_STATUS_PAUSED | PRINTER_STATUS_DOOR_OPEN |
                                   PRINTER_STATUS_OUT_OF_MEMORY |
                                   PRINTER_STATUS_NOT_AVAILABLE |
                                   PRINTER_STATUS_NO_TONER |
                                   PRINTER_STATUS_USER_INTERVENTION)) {
            status.state = IOPrinterState::Stopped;
        } else {
            status.state = IOPrinterState::Idle;
        }

        status.stateReason = StatusReason(info->Status);
        status.supplies = DoGetSupplyLevels();
        return status;
    }

    // The spooler itself has no supply levels - PRINTER_STATUS_NO_TONER is a
    // status bit, not a level - so they are asked of the driver's bidi
    // channel, and failing that, of the printer itself over IPP when the
    // queue prints to a network address. A printer neither route reaches
    // reports nothing rather than an invented number.
    std::vector<IOSupplyLevel> DoGetSupplyLevels() override {
        std::vector<IOSupplyLevel> supplies =
            QueryBidiSupplyLevels(Utf8ToWide(GetDeviceInfo().connectionPath));
#if defined(ULTRACANVAS_HAS_NET)
        if (supplies.empty()) {
            supplies = AskPrinterOverIpp();
        }
#endif
        return supplies;
    }

    IODeviceResult DoCancelJob(int jobId) override {
        PrinterHandle handle(Utf8ToWide(GetDeviceInfo().connectionPath));
        if (!handle.IsOpen()) {
            const DWORD error = GetLastError();
            return IODeviceResult::BackendError(
                IODeviceResultCode::DeviceNotFound,
                "Could not open the printer to cancel the job: " + LastErrorText(error),
                static_cast<int>(error), GetDeviceId());
        }

        if (!SetJobW(handle.Get(), static_cast<DWORD>(jobId), 0, nullptr,
                     JOB_CONTROL_CANCEL)) {
            const DWORD error = GetLastError();
            return IODeviceResult::BackendError(
                IODeviceResultCode::BackendError,
                "Could not cancel job " + std::to_string(jobId) + ": " +
                    LastErrorText(error),
                static_cast<int>(error), GetDeviceId());
        }
        return IODeviceResult::Ok(GetDeviceId());
    }

    IOPrintJobStatus DoGetJobStatus(int jobId) override {
        for (const IOPrintJobStatus& job : DoGetJobQueue()) {
            if (job.jobId == jobId) {
                return job;
            }
        }
        // Gone from the queue means finished; the spooler keeps no detail
        // once a job leaves it.
        IOPrintJobStatus status;
        status.jobId = jobId;
        status.state = IOPrintJobState::Completed;
        return status;
    }

    std::vector<IOPrintJobStatus> DoGetJobQueue() override {
        std::vector<IOPrintJobStatus> queue;

        PrinterHandle handle(Utf8ToWide(GetDeviceInfo().connectionPath));
        if (!handle.IsOpen()) {
            return queue;
        }

        DWORD needed = 0;
        DWORD returned = 0;
        EnumJobsW(handle.Get(), 0, 0xFFFFFFFF, 2, nullptr, 0, &needed, &returned);
        if (needed == 0) {
            return queue;
        }

        std::vector<uint8_t> buffer(needed);
        if (!EnumJobsW(handle.Get(), 0, 0xFFFFFFFF, 2, buffer.data(), needed, &needed,
                       &returned)) {
            return queue;
        }

        const JOB_INFO_2W* jobs = reinterpret_cast<const JOB_INFO_2W*>(buffer.data());
        for (DWORD i = 0; i < returned; ++i) {
            IOPrintJobStatus status;
            status.jobId = static_cast<int>(jobs[i].JobId);
            status.jobName = FromWide(jobs[i].pDocument);
            status.user = FromWide(jobs[i].pUserName);
            status.pagesTotal = static_cast<int>(jobs[i].TotalPages);
            status.pagesPrinted = static_cast<int>(jobs[i].PagesPrinted);
            status.stateReason = FromWide(jobs[i].pStatus);

            const DWORD jobStatus = jobs[i].Status;
            if (jobStatus & JOB_STATUS_PRINTED) {
                status.state = IOPrintJobState::Completed;
            } else if (jobStatus & JOB_STATUS_DELETED) {
                status.state = IOPrintJobState::Cancelled;
            } else if (jobStatus & JOB_STATUS_ERROR) {
                status.state = IOPrintJobState::Aborted;
            } else if (jobStatus & JOB_STATUS_PAUSED) {
                status.state = IOPrintJobState::Held;
            } else if (jobStatus & (JOB_STATUS_PRINTING | JOB_STATUS_SPOOLING)) {
                status.state = IOPrintJobState::Processing;
            } else if (jobStatus & JOB_STATUS_BLOCKED_DEVQ) {
                status.state = IOPrintJobState::Stopped;
            } else {
                status.state = IOPrintJobState::Pending;
            }
            queue.push_back(status);
        }
        return queue;
    }

    IPrintTransportPtr GetTransport() override { return SharedWindowsTransport(); }

private:
#if defined(ULTRACANVAS_HAS_NET)
    // A printer that is off costs the connect timeout per address, and
    // GetStatus() and GetSupplyLevels() both end up here, so one that did not
    // answer is left alone for a minute rather than asked twice in a row.
    static constexpr std::chrono::seconds kIppRetryAfter{60};

    // Runs under the device's mutex, like every Do* call, which also guards
    // the members it remembers.
    std::vector<IOSupplyLevel> AskPrinterOverIpp() {
        const auto now = std::chrono::steady_clock::now();
        if (ippGaveUp && now - ippGaveUpAt < kIppRetryAfter) {
            return {};
        }

        // The address that answered last time first; the port's guesses after.
        std::vector<std::string> candidates;
        if (!ippUri.empty()) {
            candidates.push_back(ippUri);
        }
        for (const std::string& uri :
             IppUrisForQueue(Utf8ToWide(GetDeviceInfo().connectionPath))) {
            if (std::find(candidates.begin(), candidates.end(), uri) == candidates.end()) {
                candidates.push_back(uri);
            }
        }

        // A host that did not take the connection will not take it on another
        // path either, so its remaining guesses are skipped.
        std::vector<std::string> unreachable;
        for (const std::string& uri : candidates) {
            const size_t hostStart = uri.find("//");
            const std::string authority =
                hostStart == std::string::npos ? uri : uri.substr(0, uri.find('/', hostStart + 2));
            if (std::find(unreachable.begin(), unreachable.end(), authority) !=
                unreachable.end()) {
                continue;
            }
            std::vector<IOSupplyLevel> supplies;
            const IODeviceResult asked = Internal::QueryIppSupplyLevels(uri, supplies);
            if (asked.success) {
                ippUri = uri;
                ippGaveUp = false;
                return supplies;
            }
            if (asked.code == IODeviceResultCode::ConnectionFailed) {
                unreachable.push_back(authority);
            }
        }

        ippUri.clear();
        ippGaveUp = true;
        ippGaveUpAt = now;
        return {};
    }

    std::string ippUri;                                  // answered last time
    bool ippGaveUp = false;
    std::chrono::steady_clock::time_point ippGaveUpAt;
#endif

    static void AddUnique(std::vector<IOPaperSize>& sizes, IOPaperSize size) {
        for (IOPaperSize existing : sizes) {
            if (existing == size) {
                return;
            }
        }
        sizes.push_back(size);
    }

    // DC_PAPERSIZE speaks tenths of a millimetre, so a size is recognised by
    // its measurements rather than by the name a driver gives it. 1 mm of
    // slack absorbs the rounding between a driver's table and the nominal
    // ISO/ANSI figure.
    static IOPaperSize PaperSizeFromTenthsMM(LONG widthTenths, LONG heightTenths) {
        constexpr int kToleranceHundredthsMM = 100;
        const int width = static_cast<int>(widthTenths) * 10;
        const int height = static_cast<int>(heightTenths) * 10;

        static const IOPaperSize kCandidates[] = {
            IOPaperSize::A3, IOPaperSize::A4, IOPaperSize::A5, IOPaperSize::A6,
            IOPaperSize::B4, IOPaperSize::B5,
            IOPaperSize::Letter, IOPaperSize::Legal, IOPaperSize::Tabloid,
            IOPaperSize::Executive,
            IOPaperSize::Photo4x6, IOPaperSize::Photo5x7, IOPaperSize::Photo8x10,
            IOPaperSize::Envelope10, IOPaperSize::EnvelopeDL, IOPaperSize::EnvelopeC5,
        };

        for (IOPaperSize candidate : kCandidates) {
            const IOPaperDimensions nominal = IOPaperSizeDimensions(candidate);
            if (std::abs(nominal.widthHundredthsMM - width) <= kToleranceHundredthsMM &&
                std::abs(nominal.heightHundredthsMM - height) <= kToleranceHundredthsMM) {
                return candidate;
            }
        }
        return IOPaperSize::Unknown;
    }

    static std::string StatusReason(DWORD status) {
        if (status == 0) return "none";
        if (status & PRINTER_STATUS_PAPER_JAM)          return "media-jam";
        if (status & PRINTER_STATUS_PAPER_OUT)          return "media-empty";
        if (status & PRINTER_STATUS_NO_TONER)           return "toner-empty";
        if (status & PRINTER_STATUS_TONER_LOW)          return "toner-low";
        if (status & PRINTER_STATUS_DOOR_OPEN)          return "door-open";
        if (status & PRINTER_STATUS_OFFLINE)            return "offline";
        if (status & PRINTER_STATUS_PAUSED)             return "paused";
        if (status & PRINTER_STATUS_USER_INTERVENTION)  return "user-intervention";
        if (status & PRINTER_STATUS_OUT_OF_MEMORY)      return "out-of-memory";
        if (status & PRINTER_STATUS_ERROR)              return "other-error";
        return "none";
    }
};

// ============================================================================
// ENUMERATION
// ============================================================================

std::vector<IODevicePtr> EnumerateWindowsPrinters() {
    std::vector<IODevicePtr> printers;

    const DWORD flags = PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS;

    DWORD needed = 0;
    DWORD returned = 0;
    EnumPrintersW(flags, nullptr, 2, nullptr, 0, &needed, &returned);
    if (needed == 0) {
        return printers;
    }

    std::vector<uint8_t> buffer(needed);
    if (!EnumPrintersW(flags, nullptr, 2, buffer.data(), needed, &needed, &returned)) {
        return printers;
    }

    const PRINTER_INFO_2W* entries =
        reinterpret_cast<const PRINTER_INFO_2W*>(buffer.data());

    for (DWORD i = 0; i < returned; ++i) {
        const PRINTER_INFO_2W& entry = entries[i];
        if (!entry.pPrinterName) {
            continue;
        }

        IODeviceInfo info;
        info.name = FromWide(entry.pPrinterName);
        info.category = IODeviceCategory::Printer;
        info.backend = "WindowsSpooler";
        info.connectionPath = info.name;

        // The queue name is the only stable identifier the spooler offers.
        // Unlike CUPS it exposes no printer UUID here, so a printer also
        // found over IPP will not collapse to one registry entry until the
        // IPP backend can match on something shared.
        info.deviceId = "winspool:" + info.name;

        info.description = FromWide(entry.pComment);
        info.location = FromWide(entry.pLocation);
        info.model = FromWide(entry.pDriverName);

        const std::string port = FromWide(entry.pPortName);
        if (!port.empty()) {
            info.attributes["port"] = port;
            if (port.rfind("USB", 0) == 0) {
                info.transport = IODeviceTransport::USB;
            } else if (port.rfind("IP_", 0) == 0 || port.rfind("WSD", 0) == 0 ||
                       port.rfind("\\\\", 0) == 0 || port.rfind("http", 0) == 0) {
                info.transport = IODeviceTransport::Network;
            } else if (port.rfind("LPT", 0) == 0) {
                info.transport = IODeviceTransport::Parallel;
            } else if (port.rfind("COM", 0) == 0) {
                info.transport = IODeviceTransport::Serial;
            }
        }
        if (entry.pServerName) {
            info.attributes["server"] = FromWide(entry.pServerName);
        }

        info.state = (entry.Status & PRINTER_STATUS_OFFLINE)
                         ? IODeviceState::Offline
                         : IODeviceState::Disconnected;

        printers.push_back(std::make_shared<WindowsPrinterDevice>(info));
    }

    return printers;
}

}  // namespace

namespace Internal {

void RegisterWindowsPrinterBackend(IODeviceManager& manager) {
    manager.RegisterEnumerator(IODeviceCategory::Printer, "WindowsSpooler",
                               EnumerateWindowsPrinters);
}

}  // namespace Internal
}  // namespace UltraCanvas

#endif  // _WIN32
