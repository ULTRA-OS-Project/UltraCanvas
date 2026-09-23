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
// source; an elevated token is required. Event payloads are read by their
// documented layout (PID, size, daddr, saddr, dport, sport, seqnum,
// connid), bounds-checked. Compiled on CI; the first elevated run is its
// acceptance test.
//
// Version: 0.5.0
// Last Modified: 2026-09-23
// Author: UltraCanvas Framework / ULTRA OS

#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0A00
#  undef _WIN32_WINNT
#  define _WIN32_WINNT 0x0A00
#endif
#if !defined(NTDDI_VERSION) || (NTDDI_VERSION >> 16) < _WIN32_WINNT
#  undef NTDDI_VERSION
#  define NTDDI_VERSION (_WIN32_WINNT << 16)
#endif

#include "NetworkMonitor/NetworkMonitorAddress.h"
#include "NetworkMonitor/NetworkMonitorEvents.h"

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

// Event ids. TCPv4: 10 send, 11 receive, 12 connect, 13 disconnect,
// 15 accept; TCPv6: 26, 27, 28, 29, 31. The UDP ids (42/43, 58/59) are
// not consumed.
constexpr USHORT kTcp4Send = 10, kTcp4Receive = 11, kTcp4Connect = 12, kTcp4Disconnect = 13, kTcp4Accept = 15;
constexpr USHORT kTcp6Send = 26, kTcp6Receive = 27, kTcp6Connect = 28, kTcp6Disconnect = 29, kTcp6Accept = 31;

struct Payload {
    uint32_t pid = 0;
    uint32_t size = 0;
    std::string remoteAddress;   // daddr
    std::string localAddress;    // saddr
    uint16_t remotePort = 0;     // dport
    uint16_t localPort = 0;      // sport
    uint64_t connectionId = 0;
};

// PID(4) size(4) daddr(4|16) saddr(4|16) dport(2) sport(2) seqnum(4) connid(pointer).
bool ReadPayload(const EVENT_RECORD& record, bool ipv6, Payload& out) {
    const auto* data = static_cast<const unsigned char*>(record.UserData);
    const std::size_t size = record.UserDataLength;
    const std::size_t addressBytes = ipv6 ? 16 : 4;
    const std::size_t pointerBytes = (record.EventHeader.Flags & EVENT_HEADER_FLAG_32_BIT_HEADER) ? 4 : 8;
    const std::size_t needed = 4 + 4 + 2 * addressBytes + 2 + 2 + 4 + pointerBytes;
    if (size < needed) return false;
    std::size_t offset = 0;
    std::memcpy(&out.pid, data + offset, 4); offset += 4;
    std::memcpy(&out.size, data + offset, 4); offset += 4;
    out.remoteAddress = ipv6 ? NetworkMonitorAddress::FormatIPv6(data + offset)
                             : NetworkMonitorAddress::FormatIPv4(data + offset);
    offset += addressBytes;
    out.localAddress = ipv6 ? NetworkMonitorAddress::FormatIPv6(data + offset)
                            : NetworkMonitorAddress::FormatIPv4(data + offset);
    offset += addressBytes;
    // Ports are in network order in the payload.
    out.remotePort = static_cast<uint16_t>((data[offset] << 8) | data[offset + 1]); offset += 2;
    out.localPort = static_cast<uint16_t>((data[offset] << 8) | data[offset + 1]); offset += 2;
    offset += 4;   // seqnum
    out.connectionId = 0;
    std::memcpy(&out.connectionId, data + offset, pointerBytes);
    return true;
}

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
            const bool denied = status == ERROR_ACCESS_DENIED;
            return NetworkMonitorResult::Error(
                denied ? NetworkMonitorResultCode::PermissionDenied : NetworkMonitorResultCode::IoError,
                "Starting the ETW session: error " + std::to_string(status) +
                (denied ? " (an elevated token is needed to trace the kernel's network events)" : ""));
        }
        status = ::EnableTraceEx2(session_, &kKernelNetworkProvider, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                                  TRACE_LEVEL_INFORMATION, 0, 0, 0, nullptr);
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
        const USHORT id = record.EventHeader.EventDescriptor.Id;
        bool ipv6 = false;
        NetworkEventKind kind;
        bool lifecycle = true;
        bool send = false;
        switch (id) {
            case kTcp4Connect:    kind = NetworkEventKind::Opened; break;
            case kTcp4Accept:     kind = NetworkEventKind::Accepted; break;
            case kTcp4Disconnect: kind = NetworkEventKind::Closed; break;
            case kTcp4Send:       lifecycle = false; send = true; kind = NetworkEventKind::Closed; break;
            case kTcp4Receive:    lifecycle = false; kind = NetworkEventKind::Closed; break;
            case kTcp6Connect:    ipv6 = true; kind = NetworkEventKind::Opened; break;
            case kTcp6Accept:     ipv6 = true; kind = NetworkEventKind::Accepted; break;
            case kTcp6Disconnect: ipv6 = true; kind = NetworkEventKind::Closed; break;
            case kTcp6Send:       ipv6 = true; lifecycle = false; send = true; kind = NetworkEventKind::Closed; break;
            case kTcp6Receive:    ipv6 = true; lifecycle = false; kind = NetworkEventKind::Closed; break;
            default: return;
        }
        Payload payload;
        if (!ReadPayload(record, ipv6, payload)) return;

        if (!lifecycle) {
            std::lock_guard<std::mutex> lock(totalsMutex_);
            Totals& totals = totals_[payload.connectionId];
            (send ? totals.sent : totals.received) += payload.size;
            if (totals_.size() > 100000) totals_.clear();   // a runaway table is worse than lost sums
            return;
        }

        NetworkConnectionEvent e;
        e.kind = kind;
        e.transport = NetworkTransport::Tcp;
        e.family = ipv6 ? NetworkAddressFamily::IPv6 : NetworkAddressFamily::IPv4;
        e.localAddress = payload.localAddress;
        e.localPort = payload.localPort;
        e.remoteAddress = payload.remoteAddress;
        e.remotePort = payload.remotePort;
        ProcessIdentity process;
        process.pid = payload.pid;
        process.displayName = "pid " + std::to_string(payload.pid);
        e.process = process;
        if (kind == NetworkEventKind::Closed) {
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
