// Tests/UltraMessage/test_main.cpp
// Entry point for the UltraMessage test binary. On Linux it first starts a
// private D-Bus session daemon so the freedesktop notification adapter (and
// the tests that drive it) never touch the user's bus; without dbus-daemon
// those tests skip.
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include <UltraMessage/UltraMessage.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#if defined(__linux__)
#  include <csignal>
#  include <sys/wait.h>
#  include <unistd.h>

namespace {

// Runs `dbus-daemon --session` with a fresh address and exports it as
// DBUS_SESSION_BUS_ADDRESS. Returns the daemon's pid, or -1 when it could
// not be started (no daemon installed).
pid_t StartPrivateDbus() {
    int pipeFds[2];
    if (pipe(pipeFds) != 0) return -1;
    const pid_t pid = fork();
    if (pid < 0) {
        close(pipeFds[0]);
        close(pipeFds[1]);
        return -1;
    }
    if (pid == 0) {
        close(pipeFds[0]);
        const std::string printAddress = "--print-address=" + std::to_string(pipeFds[1]);
        execlp("dbus-daemon", "dbus-daemon", "--session", "--nofork", "--nopidfile", "--nosyslog",
               printAddress.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    close(pipeFds[1]);
    std::string address;
    char c = 0;
    while (read(pipeFds[0], &c, 1) == 1 && c != '\n') address += c;
    close(pipeFds[0]);
    if (address.empty()) {
        int status = 0;
        waitpid(pid, &status, 0);
        return -1;
    }
    setenv("DBUS_SESSION_BUS_ADDRESS", address.c_str(), 1);
    setenv("ULTRAMSG_TEST_DBUS", "1", 1);
    return pid;
}

void StopPrivateDbus(pid_t pid) {
    if (pid <= 0) return;
    kill(pid, SIGTERM);
    int status = 0;
    waitpid(pid, &status, 0);
}

} // namespace
#endif

int main() {
    std::printf("Running UltraMessage test suite\n\n");
#if defined(__linux__)
    const pid_t dbusPid = StartPrivateDbus();
    if (dbusPid > 0)
        std::printf("  private D-Bus session at %s\n\n", std::getenv("DBUS_SESSION_BUS_ADDRESS"));
    else {
        // Never let the adapter reach the developer's real session bus.
        unsetenv("DBUS_SESSION_BUS_ADDRESS");
        unsetenv("ULTRAMSG_TEST_DBUS");
        std::printf("  dbus-daemon not available: adapter tests will be skipped\n\n");
    }
#endif
    const int rc = ultramsg_test::RunAll();
    UltraMsg_Shutdown();
#if defined(__linux__)
    StopPrivateDbus(dbusPid);
#endif
    return rc;
}
