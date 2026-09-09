// core/UltraWin/UltraWinQmp.h
// Minimal QMP (QEMU Machine Protocol) client over the machine's UNIX
// control socket: connect + capability handshake, execute a command, read
// its return object. JSON handling is the vendored yyjson engine (the same
// one UltraCanvasJSON wraps) — never exposed beyond this internal header.
// Not installed; include only from core/UltraWin/*.cpp and tests.
// Version: 0.1.0 (Stage 2a)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>

namespace ultrawin_internal {

// One QMP exchange session. Non-copyable; closes the socket on destruction.
class QmpClient {
public:
    QmpClient() = default;
    ~QmpClient();
    QmpClient(const QmpClient&) = delete;
    QmpClient& operator=(const QmpClient&) = delete;

    // Connect to the socket, read the greeting, negotiate capabilities.
    // False (with LastError set) on any failure; bounded by timeoutMs.
    bool Connect(const std::string& socketPath, int timeoutMs);

    // Execute a no-argument command ({"execute": name}) and return true
    // when QMP answered with a "return" object. Events that arrive in
    // between are skipped; an "error" answer fails with its description.
    bool Execute(const std::string& name, int timeoutMs);

    // Execute query-status and yield the "status" string ("running",
    // "paused", "shutdown", ...). Empty on failure.
    std::string QueryStatus(int timeoutMs);

    const std::string& LastError() const { return lastError; }

private:
    // Read newline-delimited JSON answers until one contains `key`
    // ("return" / "error"); the matching document (serialized) is left in
    // lastAnswer. Skips async event lines.
    bool ReadAnswer(const char* key, int timeoutMs);
    bool SendLine(const std::string& line);

    int fd = -1;
    std::string buffered;    // bytes past the last consumed line
    std::string lastAnswer;  // serialized "return" payload of the last call
    std::string lastError;
};

}  // namespace ultrawin_internal
