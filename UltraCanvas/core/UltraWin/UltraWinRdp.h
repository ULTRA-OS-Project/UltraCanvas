// core/UltraWin/UltraWinRdp.h
// RDP session against the VM-tier guest, wrapped around libfreerdp2 (the
// one UltraWin engine that IS linked — Apache-2.0). A session either
// mirrors nothing (plain connection, used by tests and health checks) or
// runs in RemoteApp (RAIL) mode, where the guest launches one program and
// exports its windows instead of a desktop. Compiled only when FreeRDP is
// available (ULTRAWIN_HAS_FREERDP); the header is safe to include always.
// Not installed; include only from core/UltraWin/*.cpp and tests.
// Version: 0.1.0 (Stage 2b)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace ultrawin_internal {

// True when UltraWin was built with libfreerdp (compile-time property of
// this build — the runtime probe the capabilities report).
bool RdpBuiltIn();

enum class RdpSessionState {
    Idle,
    Connected,     // event loop running
    Disconnected,  // ended (peer close, error after connect, or Disconnect)
    Failed         // never connected; see LastError()
};

struct RdpSessionOptions {
    std::string host = "127.0.0.1";
    int port = 3389;
    std::string username;
    std::string password;
    bool remoteApp = false;        // RAIL mode
    std::string remoteAppProgram;  // guest path or alias ("||notepad")
    std::string remoteAppArgs;
};

// One connection. Connect() blocks through the RDP/TLS/auth handshake and
// then hands the connection to an internal pump thread; Disconnect() (or
// destruction) ends it. Certificate verification is auto-accepting for
// the loopback-forwarded guest — the socket never leaves 127.0.0.1.
class RdpSession {
public:
    RdpSession() = default;
    ~RdpSession();
    RdpSession(const RdpSession&) = delete;
    RdpSession& operator=(const RdpSession&) = delete;

    bool Connect(const RdpSessionOptions& options);  // false => Failed
    void Disconnect();

    RdpSessionState State() const;
    const std::string& LastError() const { return lastError; }

private:
    void PumpLoop();

    void* instance = nullptr;  // freerdp*, opaque here
    std::thread pump;
    std::atomic<int> state{static_cast<int>(RdpSessionState::Idle)};
    std::atomic<bool> stopRequested{false};
    std::string lastError;
};

}  // namespace ultrawin_internal
