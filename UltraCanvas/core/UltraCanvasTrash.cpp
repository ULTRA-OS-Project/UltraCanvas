// core/UltraCanvasTrash.cpp
// Platform-independent half of MoveToTrash: the public entry points and the
// fallback for platforms without a trash. Every operating system call lives
// in OS/<Platform>/ (UltraCanvasWindowsTrash.cpp, UltraCanvasMacOSTrash.mm,
// UltraCanvasLinuxTrash.cpp).
// Version: 1.0.0
// Last Modified: 2026-09-24
// Author: UltraCanvas Framework

#include "UltraCanvasTrash.h"

namespace UltraCanvas {

#ifndef ULTRACANVAS_HAS_NATIVE_TRASH
    // No trash on this platform (Android, WebAssembly). Callers that asked
    // TrashAvailable() first never get here; one that did not hears why.
    bool NativeMoveToTrash(const std::string&, std::string& error) {
        error = "this system has no trash";
        return false;
    }
#endif

    bool TrashAvailable() {
#ifdef ULTRACANVAS_HAS_NATIVE_TRASH
        return true;
#else
        return false;
#endif
    }

    std::string TrashDisplayName() {
#if defined(_WIN32) || defined(_WIN64)
        return "Recycle Bin";
#else
        return "Trash";
#endif
    }

    bool MoveToTrash(const std::string& path, std::string& error) {
        error.clear();
        if (path.empty()) {
            error = "no file was named";
            return false;
        }
        if (!NativeMoveToTrash(path, error)) {
            if (error.empty()) error = "the system refused";
            return false;
        }
        return true;
    }

} // namespace UltraCanvas
