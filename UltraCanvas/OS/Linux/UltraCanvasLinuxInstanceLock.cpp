// OS/Linux/UltraCanvasLinuxInstanceLock.cpp
// Linux (and BSD) backend for UltraCanvasInstanceLock: flock() on the lock file.
//
// flock() belongs to the open file, and the kernel drops it when the last
// descriptor closes - at exit, at a crash, at a kill -9 - so the lock never
// outlives the process that took it. The file keeps the holder's process id,
// for a person looking at it; nothing reads it back. The same code serves
// OS/Linux and OS/MacOS, which are separate platform folders.
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "UltraCanvasInstanceLock.h"

#include <cerrno>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace UltraCanvas {

    InstanceLockResult NativeAcquireInstanceLock(const std::string& path, intptr_t& handle,
                                                 std::string& error) {
        // POSIX paths are bytes: the UTF-8 name is the name.
        const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (fd < 0) {
            error = "cannot open " + path + ": " + std::strerror(errno);
            return InstanceLockResult::Failed;
        }
        if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
            const int code = errno;
            ::close(fd);
            if (code == EWOULDBLOCK) return InstanceLockResult::HeldElsewhere;
            error = "cannot lock " + path + ": " + std::strerror(code);
            return InstanceLockResult::Failed;
        }
        const std::string pid = std::to_string(static_cast<long>(::getpid())) + "\n";
        if (::ftruncate(fd, 0) == 0) {
            const ssize_t written = ::write(fd, pid.data(), pid.size());
            (void)written;   // informational only
        }
        handle = fd;
        return InstanceLockResult::Acquired;
    }

    void NativeReleaseInstanceLock(intptr_t handle) {
        const int fd = static_cast<int>(handle);
        ::flock(fd, LOCK_UN);
        ::close(fd);
    }

} // namespace UltraCanvas
