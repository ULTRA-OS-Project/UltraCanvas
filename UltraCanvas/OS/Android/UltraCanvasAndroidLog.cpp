// OS/Android/UltraCanvasAndroidLog.cpp
// stdout/stderr -> logcat pump. See UltraCanvasAndroidLog.h for why.
// Version: 1.0.0
// Last Modified: 2026-09-07
// Author: UltraCanvas Framework

#include "UltraCanvasAndroidLog.h"

#include <android/log.h>

#include <cerrno>
#include <cstdio>
#include <string>
#include <thread>
#include <unistd.h>

namespace UltraCanvas {

    namespace {

        constexpr const char* kStdioTag = "UltraCanvas-stdio";

        // logcat truncates a message around 4 KB. Splitting earlier keeps a
        // long line readable instead of silently clipped, and bounds the
        // buffer against output that never sends a newline (a progress bar
        // drawn with '\r', for instance).
        constexpr std::string::size_type kMaxLine = 3800;

        void Emit(std::string& line) {
            if (line.empty()) return;
            __android_log_write(ANDROID_LOG_INFO, kStdioTag, line.c_str());
            line.clear();
        }

        // Reads until the write ends close, which for stdout/stderr means
        // process exit - so this loop is the thread's whole life.
        void PumpLoop(int readFd) {
            std::string line;
            char        buffer[512];

            for (;;) {
                const ssize_t got = ::read(readFd, buffer, sizeof(buffer));
                if (got > 0) {
                    for (ssize_t i = 0; i < got; ++i) {
                        const char c = buffer[i];
                        if (c == '\n') {
                            Emit(line);
                        } else if (c != '\r') {
                            line.push_back(c);
                            if (line.size() >= kMaxLine) Emit(line);
                        }
                    }
                    continue;
                }
                if (got < 0 && errno == EINTR) continue;
                break;
            }

            Emit(line);          // whatever was left unterminated
            ::close(readFd);
        }

    } // namespace

    bool RedirectStdioToLogcat() {
        static bool installed = false;
        static bool succeeded = false;
        if (installed) return succeeded;
        installed = true;

        int fds[2] = { -1, -1 };
        if (::pipe(fds) != 0) {
            return false;       // descriptors untouched
        }

        // A pipe makes stdout fully buffered, which would hold output back
        // until 4 KB accumulated or the process exited - precisely the wrong
        // behaviour when the thing being diagnosed is a crash. stderr is
        // unbuffered already; say so anyway so neither depends on the
        // implementation's default.
        ::setvbuf(stdout, nullptr, _IOLBF, 0);
        ::setvbuf(stderr, nullptr, _IONBF, 0);

        ::dup2(fds[1], STDOUT_FILENO);
        ::dup2(fds[1], STDERR_FILENO);
        ::close(fds[1]);        // the two dups above own the write end now

        std::thread(PumpLoop, fds[0]).detach();

        succeeded = true;
        return true;
    }

} // namespace UltraCanvas
