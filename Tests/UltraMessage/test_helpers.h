// Tests/UltraMessage/test_helpers.h
// Helpers shared by the UltraMessage test files: the private bus path of
// this test process, a Connect that throws on failure, a callback pump.
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "test_framework.h"

#include <UltraMessage/UltraMessage.h>

#include <chrono>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>

#ifdef _WIN32
#  include <process.h>
#  define ULTRAMSG_TEST_GETPID _getpid
#else
#  include <unistd.h>
#  define ULTRAMSG_TEST_GETPID getpid
#endif

namespace ultramsg_test {

// A private bus per test process, so a broker of the user's own never
// answers and parallel test runs never meet.
inline std::string TestBusPath() {
    static const std::string path = [] {
        const std::string pid = std::to_string(ULTRAMSG_TEST_GETPID());
#ifdef _WIN32
        return "\\\\.\\pipe\\UltraMessageTest-" + pid;
#else
        const char* tmp = std::getenv("TMPDIR");
        std::string base = tmp && *tmp ? tmp : "/tmp";
        return base + "/ultramsg-test-" + pid + "/bus.sock";
#endif
    }();
    return path;
}

inline UltraMsgHandle Connect(const std::string& appId, const std::string& name = "") {
    UltraMsgConnectOptions options;
    options.appId = appId;
    options.displayName = name.empty() ? appId : name;
    options.busPath = TestBusPath();
    options.journalPath = ":memory:";
    UltraMsgResult error;
    UltraMsgHandle handle = UltraMsg_Connect(options, &error);
    if (handle == UltraMsgInvalidHandle)
        throw Failure{"connect " + appId + " failed: " + error.message};
    return handle;
}

// Pumps queued callbacks until `done` holds or `timeout` elapses.
inline bool WaitFor(const std::function<bool()>& done,
                    std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        UltraMsg_ProcessPending();
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    UltraMsg_ProcessPending();
    return done();
}

struct Scoped {
    UltraMsgHandle handle;
    ~Scoped() { UltraMsg_Disconnect(handle); }
};

} // namespace ultramsg_test
