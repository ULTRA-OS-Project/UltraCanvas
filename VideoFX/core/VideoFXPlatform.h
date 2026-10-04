// VideoFX/core/VideoFXPlatform.h
// Internal: the few things VideoFX asks of the operating system. One
// implementation per platform under VideoFX/OS/<Platform>/.
// Version: 0.2.0
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework
#pragma once

#include <string>

namespace VideoFX {
namespace Internal {

// UTF-8 directory of the running executable, no trailing separator; "" if unknown
std::string ExecutableDir();

} // namespace Internal
} // namespace VideoFX
