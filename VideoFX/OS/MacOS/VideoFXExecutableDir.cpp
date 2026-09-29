// VideoFX/OS/MacOS/VideoFXExecutableDir.cpp
// Directory of the running executable (macOS, symlinks resolved): inside an
// app bundle that is Contents/MacOS, next to Contents/Resources.
// Version: 0.2.0
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework

#include "VideoFXPlatform.h"

#include <climits>
#include <cstdint>
#include <cstdlib>
#include <mach-o/dyld.h>
#include <string>
#include <vector>

namespace VideoFX {
namespace Internal {

std::string ExecutableDir() {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> raw(size + 1, '\0');
    if (_NSGetExecutablePath(raw.data(), &size) != 0) return "";
    char resolved[PATH_MAX];
    std::string path = realpath(raw.data(), resolved) ? resolved : raw.data();
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? "" : path.substr(0, slash);
}

} // namespace Internal
} // namespace VideoFX
