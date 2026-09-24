// OS/MSWindows/UltraCanvasWindowsNetworkMonitorDns.cpp
// The Windows DNS client's own events as a NetworkMonitor name source: a
// real-time ETW session on Microsoft-Windows-DNS-Client, whose event 3008
// ("DNS query completed") carries the name asked for, the answers, and -
// in the event header - the PID that asked. It hooks the resolver, not the
// wire, so it sees queries the proxy would (every client of the system
// resolver) and gives the process the proxy cannot; it misses, like every
// other source, a browser resolving over HTTPS on its own.
//
// Documented Win32 only: StartTrace / EnableTraceEx2 / OpenTrace /
// ProcessTrace. Starting a session needs an elevated token (or membership
// of Performance Log Users); without it Start reports PermissionDenied and
// the caller falls back to the proxy. The event's payload is read by its
// manifest layout (QueryName, QueryType, QueryOptions, QueryStatus,
// QueryResults) with every step bounds-checked, rather than through TDH,
// which would need the manifest at run time.
//
// Version: 0.4.0
// Last Modified: 2026-09-22
// Author: UltraCanvas Framework / ULTRA OS

#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0A00
#  undef _WIN32_WINNT
#  define _WIN32_WINNT 0x0A00
#endif
#if !defined(NTDDI_VERSION) || (NTDDI_VERSION >> 16) < _WIN32_WINNT
#  undef NTDDI_VERSION
#  define NTDDI_VERSION (_WIN32_WINNT << 16)
#endif

#include "NetworkMonitor/NetworkMonitorDns.h"
#include "NetworkMonitor/NetworkMonitorNames.h"
#include "NetworkMonitor/NetworkMonitorStore.h"

#include <atomic>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <string>
#include <thread>
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

// {1C95126E-7EEA-49A9-A3FE-A378B03DDB4D} - Microsoft-Windows-DNS-Client.
const GUID kDnsClientProvider = { 0x1C95126E, 0x7EEA, 0x49A9,
                                  { 0xA3, 0xFE, 0xA3, 0x78, 0xB0, 0x3D, 0xDB, 0x4D } };
constexpr USHORT kQueryCompletedEvent = 3008;
const wchar_t* const kSessionName = L"UltraCanvas-NetworkMonitor-DNS";

std::string Utf8FromWide(const std::wstring& text) {
    if (text.empty()) return std::string();
    const int needed = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                             nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(needed), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), needed,
                          nullptr, nullptr);
    return out;
}

// A NUL-terminated UTF-16 string at `offset` in the payload; false when it
// runs off the end. Advances `offset` past the terminator.
bool ReadWide(const unsigned char* data, std::size_t size, std::size_t& offset, std::wstring& out) {
    out.clear();
    while (offset + 2 <= size) {
        wchar_t c;
        std::memcpy(&c, data + offset, 2);
        offset += 2;
        if (c == L'\0') return true;
        out += c;
    }
    return false;
}

// QueryResults is a ';'-separated list where address entries are plain
// ("93.184.216.34", "2606:2800::1") or IPv4-mapped ("::ffff:93.184.216.34"),
// and CNAME entries read "type: 5 host.example.com". Only the addresses
// name the query here; CNAMEs are already resolved by the time the event
// fires.
void ParseResults(const std::string& results, std::vector<std::string>& addresses) {
    std::size_t start = 0;
    while (start < results.size()) {
        std::size_t end = results.find(';', start);
        if (end == std::string::npos) end = results.size();
        std::string entry = results.substr(start, end - start);
        start = end + 1;
        while (!entry.empty() && (entry.front() == ' ')) entry.erase(entry.begin());
        while (!entry.empty() && (entry.back() == ' ')) entry.pop_back();
        if (entry.empty() || entry.rfind("type:", 0) == 0) continue;
        if (entry.rfind("::ffff:", 0) == 0 && entry.find('.') != std::string::npos) entry.erase(0, 7);
        unsigned char bytes[16];
        if (::inet_pton(AF_INET, entry.c_str(), bytes) == 1 || ::inet_pton(AF_INET6, entry.c_str(), bytes) == 1) {
            addresses.push_back(entry);
        }
    }
}

class EtwDnsSource : public INameSource {
public:
    ~EtwDnsSource() override { Stop(); }

    std::string Name() const override { return "Windows DNS client events (ETW)"; }
    NameSource  Kind() const override { return NameSource::EtwDnsClient; }
    bool        ReportsProcess() const override { return true; }
    bool        IsRunning() const override { return running_.load(); }
    std::string LastError() const override {
        std::lock_guard<std::mutex> lock(errorMutex_);
        return lastError_;
    }

    NetworkMonitorResult Start(std::function<void(const DnsObservation&)> onObservation) override {
        if (running_.load()) return NetworkMonitorResult::Ok();
        onObservation_ = std::move(onObservation);

        // The properties block carries the session name after the struct.
        const std::size_t nameBytes = (std::wcslen(kSessionName) + 1) * sizeof(wchar_t);
        properties_.assign(sizeof(EVENT_TRACE_PROPERTIES) + nameBytes, 0);
        auto* properties = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(properties_.data());
        properties->Wnode.BufferSize = static_cast<ULONG>(properties_.size());
        properties->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        properties->Wnode.ClientContext = 1;   // QPC timestamps
        properties->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
        properties->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

        ULONG status = ::StartTraceW(&session_, kSessionName, properties);
        if (status == ERROR_ALREADY_EXISTS) {
            // A session an earlier run left behind: take it down and retry.
            ::ControlTraceW(0, kSessionName, properties, EVENT_TRACE_CONTROL_STOP);
            properties = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(properties_.data());
            std::memset(properties_.data(), 0, properties_.size());
            properties->Wnode.BufferSize = static_cast<ULONG>(properties_.size());
            properties->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
            properties->Wnode.ClientContext = 1;
            properties->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
            properties->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
            status = ::StartTraceW(&session_, kSessionName, properties);
        }
        if (status != ERROR_SUCCESS) {
            session_ = 0;
            const bool denied = status == ERROR_ACCESS_DENIED;
            return NetworkMonitorResult::Error(
                denied ? NetworkMonitorResultCode::PermissionDenied : NetworkMonitorResultCode::IoError,
                std::string("Starting the ETW session: error ") + std::to_string(status) +
                (denied ? " (an elevated token is needed to trace the DNS client)" : ""));
        }
        status = ::EnableTraceEx2(session_, &kDnsClientProvider, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                                  TRACE_LEVEL_INFORMATION, 0, 0, 0, nullptr);
        if (status != ERROR_SUCCESS) {
            ::ControlTraceW(session_, nullptr, properties, EVENT_TRACE_CONTROL_STOP);
            session_ = 0;
            return NetworkMonitorResult::Error(NetworkMonitorResultCode::IoError,
                "Enabling the DNS client provider: error " + std::to_string(status));
        }

        EVENT_TRACE_LOGFILEW logFile{};
        logFile.LoggerName = const_cast<wchar_t*>(kSessionName);
        logFile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
        logFile.EventRecordCallback = &EtwDnsSource::OnEvent;
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
            // Blocks until the session is stopped or the trace closed.
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
    static void WINAPI OnEvent(EVENT_RECORD* record) {
        auto* self = static_cast<EtwDnsSource*>(record->UserContext);
        if (!self || !self->running_.load()) return;
        if (record->EventHeader.EventDescriptor.Id != kQueryCompletedEvent) return;
        if (!IsEqualGUID(record->EventHeader.ProviderId, kDnsClientProvider)) return;
        self->Handle(*record);
    }

    void Handle(const EVENT_RECORD& record) {
        const auto* data = static_cast<const unsigned char*>(record.UserData);
        const std::size_t size = record.UserDataLength;
        std::size_t offset = 0;
        std::wstring queryName, queryResults;
        if (!ReadWide(data, size, offset, queryName)) return;
        if (offset + 4 + 8 + 4 > size) return;
        uint32_t queryStatus = 0;
        std::memcpy(&queryStatus, data + offset + 4 + 8, 4);
        offset += 4 + 8 + 4;
        if (queryStatus != 0) return;   // NXDOMAIN and friends name nothing
        if (!ReadWide(data, size, offset, queryResults)) return;

        DnsObservation observation;
        observation.queryName = NetworkMonitorDns::NormalizeName(Utf8FromWide(queryName));
        ParseResults(Utf8FromWide(queryResults), observation.addresses);
        if (observation.queryName.empty() || observation.addresses.empty()) return;
        observation.source = NameSource::EtwDnsClient;
        observation.observedAt = NetworkMonitor_Now();
        ProcessIdentity process;
        process.pid = record.EventHeader.ProcessId;
        process.displayName = "pid " + std::to_string(process.pid);
        observation.process = process;
        if (onObservation_) onObservation_(observation);
    }

    std::function<void(const DnsObservation&)> onObservation_;
    std::vector<unsigned char> properties_;
    TRACEHANDLE session_ = 0;
    TRACEHANDLE trace_ = 0;
    std::thread worker_;
    std::atomic<bool> running_{false};
    mutable std::mutex errorMutex_;
    std::string lastError_;
};

} // namespace

std::unique_ptr<INameSource> NetworkMonitor_CreateSystemDnsSource() {
    return std::make_unique<EtwDnsSource>();
}

} // namespace UltraCanvas
