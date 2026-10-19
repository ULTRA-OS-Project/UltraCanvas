// VideoFX/core/VideoFXPlatform.h
// Internal: the few things VideoFX asks of the operating system. One
// implementation per platform under VideoFX/OS/<Platform>/.
// Version: 0.11.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#pragma once

#include <string>

namespace VideoFX {
namespace Internal {

// UTF-8 directory of the running executable, no trailing separator; "" if unknown
std::string ExecutableDir();

// UTF-8 per-user cache folder (where proxies go), no trailing separator; ""
// if the system names none. Always built: the FFmpeg-free stub has it too.
std::string UserCacheDir();

} // namespace Internal
} // namespace VideoFX
