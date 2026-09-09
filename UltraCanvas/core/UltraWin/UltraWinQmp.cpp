// core/UltraWin/UltraWinQmp.cpp
// QMP client implementation. The protocol is newline-delimited JSON on a
// UNIX socket: a greeting ({"QMP": ...}) on connect, then request/response
// with interleaved async events. Only the tiny subset UltraWin needs is
// implemented: qmp_capabilities, no-argument commands, query-status.
// Version: 0.1.0 (Stage 2a)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWinQmp.h"

#include "yyjson.h"

#include <chrono>
#include <cstring>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace ultrawin_internal {

namespace {

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

QmpClient::~QmpClient() {
    if (fd >= 0) close(fd);
}

bool QmpClient::Connect(const std::string& socketPath, int timeoutMs) {
    lastError.clear();
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        lastError = "socket() failed";
        return false;
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (socketPath.size() >= sizeof(addr.sun_path)) {
        lastError = "socket path too long";
        return false;
    }
    std::strncpy(addr.sun_path, socketPath.c_str(),
                 sizeof(addr.sun_path) - 1);

    // QEMU creates the socket asynchronously after spawn — retry within
    // the deadline instead of failing on the first ECONNREFUSED/ENOENT.
    const int64_t deadline = NowMs() + timeoutMs;
    while (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) !=
           0) {
        if (NowMs() >= deadline) {
            lastError = "QMP socket not accepting connections";
            return false;
        }
        struct timespec ts{0, 100 * 1000 * 1000};
        nanosleep(&ts, nullptr);
    }

    if (!ReadAnswer("QMP", static_cast<int>(deadline - NowMs())))
        return false;
    if (!SendLine("{\"execute\":\"qmp_capabilities\"}")) return false;
    return ReadAnswer("return", static_cast<int>(deadline - NowMs()));
}

bool QmpClient::Execute(const std::string& name, int timeoutMs) {
    lastError.clear();
    // Command names come from our own code (fixed strings) — still escape
    // nothing and refuse anything that could break the JSON framing.
    if (name.find_first_of("\"\\\n") != std::string::npos) {
        lastError = "invalid command name";
        return false;
    }
    if (!SendLine("{\"execute\":\"" + name + "\"}")) return false;
    return ReadAnswer("return", timeoutMs);
}

std::string QmpClient::QueryStatus(int timeoutMs) {
    if (!Execute("query-status", timeoutMs)) return {};
    yyjson_doc* doc =
        yyjson_read(lastAnswer.c_str(), lastAnswer.size(), 0);
    if (!doc) return {};
    std::string status;
    if (yyjson_val* s =
            yyjson_obj_get(yyjson_doc_get_root(doc), "status")) {
        if (const char* str = yyjson_get_str(s)) status = str;
    }
    yyjson_doc_free(doc);
    return status;
}

bool QmpClient::SendLine(const std::string& line) {
    std::string framed = line + "\n";
    size_t off = 0;
    while (off < framed.size()) {
        ssize_t n = write(fd, framed.data() + off, framed.size() - off);
        if (n <= 0) {
            lastError = "QMP write failed";
            return false;
        }
        off += static_cast<size_t>(n);
    }
    return true;
}

bool QmpClient::ReadAnswer(const char* key, int timeoutMs) {
    const int64_t deadline = NowMs() + (timeoutMs > 0 ? timeoutMs : 1);
    for (;;) {
        // Consume complete lines already buffered.
        size_t nl;
        while ((nl = buffered.find('\n')) != std::string::npos) {
            std::string line = buffered.substr(0, nl);
            buffered.erase(0, nl + 1);
            if (line.empty()) continue;
            yyjson_doc* doc = yyjson_read(line.c_str(), line.size(), 0);
            if (!doc) continue;  // fragment noise — skip
            yyjson_val* root = yyjson_doc_get_root(doc);
            if (yyjson_val* err = yyjson_obj_get(root, "error")) {
                yyjson_val* desc = yyjson_obj_get(err, "desc");
                lastError = desc && yyjson_get_str(desc)
                                ? yyjson_get_str(desc)
                                : "QMP error";
                yyjson_doc_free(doc);
                return false;
            }
            if (yyjson_val* val = yyjson_obj_get(root, key)) {
                size_t len = 0;
                char* json = yyjson_val_write(val, 0, &len);
                lastAnswer = json ? std::string(json, len) : "{}";
                free(json);
                yyjson_doc_free(doc);
                return true;
            }
            yyjson_doc_free(doc);  // an async event — keep reading
        }

        int64_t remaining = deadline - NowMs();
        if (remaining <= 0) {
            lastError = "QMP answer timed out";
            return false;
        }
        struct pollfd pfd{fd, POLLIN, 0};
        int pr = poll(&pfd, 1, static_cast<int>(remaining));
        if (pr <= 0) {
            lastError = "QMP answer timed out";
            return false;
        }
        char buf[4096];
        ssize_t n = read(fd, buf, sizeof buf);
        if (n <= 0) {
            lastError = "QMP connection closed";
            return false;
        }
        buffered.append(buf, static_cast<size_t>(n));
    }
}

}  // namespace ultrawin_internal
