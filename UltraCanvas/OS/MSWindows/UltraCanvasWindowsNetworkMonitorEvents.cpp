// OS/MSWindows/UltraCanvasWindowsNetworkMonitorEvents.cpp
// The Windows kernel's network events as a NetworkMonitor event source: a
// real-time ETW session on Microsoft-Windows-Kernel-Network, whose events
// carry the PID with every TCP connect, accept, disconnect, send and
// receive. Connect and accept become Opened and Accepted; sends and
// receives are summed per connection and reported with its Closed, which
// gives Windows the per-connection byte counters the IP Helper backend
// cannot (the proposal's §3.2). UDP has no lifecycle here and is not
// reported.
//
// Documented Win32 only, the same session mechanics as the DNS client
// source; an elevated token is required. Event payloads are parsed by
// NetworkMonitorKernelNetwork (pure, tested from fixture bytes on every
// platform), each by its own template. Compiled on CI; the first elevated
// run is its acceptance test - scripts/netmon-etw-acceptance.ps1 runs it.
//
// Version: 0.9.0
// Last Modified: 2026-11-11
// Author: UltraCanvas Framework / ULTRA OS

#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0A00
#  undef _WIN32_WINNT
#  define _WIN32_WINNT 0x0A00
#endif
#if !defined(NTDDI_VERSION) || (NTDDI_VERSION >> 16) < _WIN32_WINNT
#  undef NTDDI_VERSION
#  define NTDDI_VERSION (_WIN32_WINNT << 16)
#endif

#include "NetworkMonitor/NetworkMonitorEvents.h"
#include "NetworkMonitor/NetworkMonitorKernelNetwork.h"
#include "UltraCanvasWindowsProcessNames.h"

#include <atomic>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>

namespace UltraCanvas {
namespace {

// {7DD42A49-5329-4832-8DFD-43D979153A88} - Microsoft-Windows-Kernel-Network.
const GUID kKernelNetworkProvider = { 0x7DD42A49, 0x5329, 0x4832,
                                      { 0x8D, 0xFD, 0x43, 0xD9, 0x79, 0x15, 0x3A, 0x88 } };
const wchar_t* const kSessionName = L"UltraCanvas-NetworkMonitor-Network";

class KernelNetworkEventSource : public IConnectionEventSource {
public:
    ~KernelNetworkEventSource() override { Stop(); }

    std::string Name() const override { return "Windows kernel network events (ETW)"; }
    bool ReportsProcess() const override { return true; }
    bool ReportsBytes() const override { return true; }
    bool IsRunning() const override { return running_.load(); }
    std::string LastError() const override {
        std::lock_guard<std::mutex> lock(errorMutex_);
        return lastError_;
    }

    NetworkMonitorResult Start(std::function<void(const NetworkConnectionEvent&)> onEvent) override {
        if (running_.load()) return NetworkMonitorResult::Ok();
        onEvent_ = std::move(onEvent);

        const std::size_t nameBytes = (std::wcslen(kSessionName) + 1) * sizeof(wchar_t);
        properties_.assign(sizeof(EVENT_TRACE_PROPERTIES) + nameBytes, 0);
        auto fill = [this] {
            auto* p = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(properties_.data());
            std::memset(properties_.data(), 0, properties_.size());
            p->Wnode.BufferSize = static_cast<ULONG>(properties_.size());
            p->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
            p->Wnode.ClientContext = 1;
            p->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
            p->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
            return p;
        };
        EVENT_TRACE_PROPERTIES* properties = fill();
        ULONG status = ::StartTraceW(&session_, kSessionName, properties);
        if (status == ERROR_ALREADY_EXISTS) {
            ::ControlTraceW(0, kSessionName, properties, EVENT_TRACE_CONTROL_STOP);
            properties = fill();
            status = ::StartTraceW(&session_, kSessionName, properties);
        }
        if (status != ERROR_SUCCESS) {
            session_ = 0;
            // Access denied is the everyday case - the monitor was started
            // unelevated - so it says what to do, not what Windows said.
            if (status == ERROR_ACCESS_DENIED) {
                return NetworkMonitorResult::Error(NetworkMonitorResultCode::PermissionDenied,
                    "Needs an elevated run: Windows lets only an administrator trace the kernel's network "
                    "events, so only the snapshot differ reports events until the monitor is run as administrator");
            }
            return NetworkMonitorResult::Error(NetworkMonitorResultCode::IoError,
                "Starting the ETW session: error " + std::to_string(status));
        }
        // Every keyword: the provider files IPv4 and IPv6 under keywords of
        // their own, and a mask of zero is "all" only by a convention this
        // does not rely on.
        status = ::EnableTraceEx2(session_, &kKernelNetworkProvider, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                                  TRACE_LEVEL_INFORMATION, ~0ULL, 0, 0, nullptr);
        if (status != ERROR_SUCCESS) {
            ::ControlTraceW(session_, nullptr, properties, EVENT_TRACE_CONTROL_STOP);
            session_ = 0;
            return NetworkMonitorResult::Error(NetworkMonitorResultCode::IoError,
                "Enabling the kernel network provider: error " + std::to_string(status));
        }
        EVENT_TRACE_LOGFILEW logFile{};
        logFile.LoggerName = const_cast<wchar_t*>(kSessionName);
        logFile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
        logFile.EventRecordCallback = &KernelNetworkEventSource::OnEvent;
        logFile.Context = this;
        trace_ = ::OpenTraceW(&logFile);
        if (trace_ == INVALID_PROCESSTRACE_HANDLE) {
            const DWORD error = ::GetLastError();
            ::ControlTraceW(session_, nullptr, properties, EVENT_TRACE_CONTROL_STOP);
            session_ = 0;
            trace_ = 0;
            return NetworkMonitorResult::Error(NetworkMonitorResultCode::IoError,
                "Opening the trace: error " + std::to_string(error));
        }
        running_ = true;
        worker_ = std::thread([this] {
            const ULONG rc = ::ProcessTrace(&trace_, 1, nullptr, nullptr);
            if (rc != ERROR_SUCCESS && rc != ERROR_CANCELLED && running_.load()) {
                std::lock_guard<std::mutex> lock(errorMutex_);
                lastError_ = "The trace ended: error " + std::to_string(rc);
            }
        });
        return NetworkMonitorResult::Ok();
    }

    void Stop() override {
        if (!running_.load()) return;
        running_ = false;
        auto* properties = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(properties_.data());
        ::ControlTraceW(session_, nullptr, properties, EVENT_TRACE_CONTROL_STOP);
        ::CloseTrace(trace_);
        if (worker_.joinable()) worker_.join();
        session_ = 0;
        trace_ = 0;
    }

private:
    struct Totals { uint64_t sent = 0; uint64_t received = 0; };

    static void WINAPI OnEvent(EVENT_RECORD* record) {
        auto* self = static_cast<KernelNetworkEventSource*>(record->UserContext);
        if (!self || !self->running_.load()) return;
        if (!IsEqualGUID(record->EventHeader.ProviderId, kKernelNetworkProvider)) return;
        self->Handle(*record);
    }

    void Handle(const EVENT_RECORD& record) {
        namespace knet = NetworkMonitorKernelNetwork;
        using Kind = knet::Kind;
        const std::size_t pointerBytes = (record.EventHeader.Flags & EVENT_HEADER_FLAG_32_BIT_HEADER) ? 4 : 8;
        knet::Event payload;
        if (!knet::Parse(record.EventHeader.EventDescriptor.Id, static_cast<const unsigned char*>(record.UserData),
                         record.UserDataLength, pointerBytes, payload)) return;

        if (payload.kind == Kind::Send || payload.kind == Kind::Receive) {
            std::lock_guard<std::mutex> lock(totalsMutex_);
            Totals& totals = totals_[payload.connectionId];
            (payload.kind == Kind::Send ? totals.sent : totals.received) += payload.size;
            if (totals_.size() > 100000) totals_.clear();   // a runaway table is worse than lost sums
            return;
        }

        NetworkConnectionEvent e;
        e.kind = payload.kind == Kind::Connect ? NetworkEventKind::Opened
               : payload.kind == Kind::Accept  ? NetworkEventKind::Accepted
                                               : NetworkEventKind::Closed;
        e.transport = NetworkTransport::Tcp;
        e.family = payload.ipv6 ? NetworkAddressFamily::IPv6 : NetworkAddressFamily::IPv4;
        e.localAddress = payload.localAddress;
        e.localPort = payload.localPort;
        e.remoteAddress = payload.remoteAddress;
        e.remotePort = payload.remotePort;
        // The event carries the PID; the process list names it, no handle
        // needed. The registry adds the path and user from the socket
        // table where it has them; "pid N" only for a process already gone.
        ProcessIdentity process;
        process.pid = payload.pid;
        process.displayName = NetworkMonitor_WindowsProcessName(payload.pid, true);
        if (process.displayName.empty()) process.displayName = "pid " + std::to_string(payload.pid);
        e.process = process;
        if (e.kind == NetworkEventKind::Closed) {
            std::lock_guard<std::mutex> lock(totalsMutex_);
            auto found = totals_.find(payload.connectionId);
            if (found != totals_.end()) {
                e.bytesSent = found->second.sent;
                e.bytesReceived = found->second.received;
                totals_.erase(found);
            } else {
                e.bytesSent = 0;
                e.bytesReceived = 0;
            }
        }
        e.observedAtMs = NetworkMonitor_NowMs();
        e.sourceName = Name();
        if (onEvent_) onEvent_(e);
    }

    std::function<void(const NetworkConnectionEvent&)> onEvent_;
    std::vector<unsigned char> properties_;
    TRACEHANDLE session_ = 0;
    TRACEHANDLE trace_ = 0;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::mutex totalsMutex_;
    std::unordered_map<uint64_t, Totals> totals_;
    mutable std::mutex errorMutex_;
    std::string lastError_;
};

} // namespace

std::unique_ptr<IConnectionEventSource> NetworkMonitor_CreateSystemEventSource() {
    return std::make_unique<KernelNetworkEventSource>();
}

} // namespace UltraCanvas
