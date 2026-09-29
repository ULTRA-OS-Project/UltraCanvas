// VideoFX/OS/Linux/VideoFXExecutableDir.cpp
// Directory of the running executable (Linux / Unix): where bundled
// resources such as media/fonts are looked for.
// Version: 0.2.0
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework

#include "VideoFXPlatform.h"

#include <climits>
#include <string>
#include <unistd.h>

namespace VideoFX {
namespace Internal {

std::string ExecutableDir() {
    char buf[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return "";
    std::string path(buf, static_cast<size_t>(n));
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? "" : path.substr(0, slash);
}

} // namespace Internal
} // namespace VideoFX
