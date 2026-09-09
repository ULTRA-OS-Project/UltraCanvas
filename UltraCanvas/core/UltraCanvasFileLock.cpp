// core/UltraCanvasFileLock.cpp
// Platform-independent half of the file-lock probe: the public entry points,
// the fallback for platforms with no backend, and the wording every display
// of the result shares. Every operating system call lives in OS/<Platform>/.
// Version: 1.0.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework

#include "UltraCanvasFileLock.h"

namespace UltraCanvas {

#ifndef ULTRACANVAS_HAS_NATIVE_FILE_LOCK
    // No backend on this platform: every probe answers Unknown, and callers
    // that asked FileLockProbeAvailable() first never got here. The platforms
    // that do have one define the symbol in their own file and CMake sets
    // ULTRACANVAS_HAS_NATIVE_FILE_LOCK there.
    bool NativeProbeFileLocks(const std::vector<std::string>&, bool,
                              std::vector<FileLockInfo>&) {
        return false;
    }
#endif

    bool FileLockProbeAvailable() {
#ifdef ULTRACANVAS_HAS_NATIVE_FILE_LOCK
        return true;
#else
        return false;
#endif
    }

    std::vector<FileLockInfo> ProbeFileLocks(const std::vector<std::string>& paths,
                                             bool wantHolders) {
        // Unknown for every path is the honest answer without a backend, and
        // the shape callers index into either way.
        std::vector<FileLockInfo> out(paths.size());
        if (paths.empty()) return out;
        if (!NativeProbeFileLocks(paths, wantHolders, out))
            out.assign(paths.size(), FileLockInfo{});
        else if (out.size() != paths.size())
            out.resize(paths.size());   // a backend that answered short
        return out;
    }

    FileLockInfo ProbeFileLock(const std::string& path, bool wantHolders) {
        if (path.empty()) return FileLockInfo{};
        return ProbeFileLocks(std::vector<std::string>{path}, wantHolders).front();
    }

    char FileLockAttributeLetter(FileLockState state) {
        switch (state) {
            case FileLockState::Locked:        return 'X';
            case FileLockState::OpenElsewhere: return 'O';
            default:                           return 0;
        }
    }

    std::string FileLockText(const FileLockInfo& info) {
        if (!info.InUse()) return "";

        std::string who;
        for (const std::string& holder : info.holders) {
            if (holder.empty()) continue;
            if (!who.empty()) who += ", ";
            who += holder;
        }

        if (info.state == FileLockState::OpenElsewhere) {
            // Not a warning: on POSIX this is the everyday state of a file
            // something is reading, and it stops nothing.
            return who.empty() ? "Open in another program"
                               : "Open in " + who;
        }

        std::string text = who.empty() ? "In use by another program"
                                       : "In use by " + who;
        // Which half of the problem it is, when only one half applies: a file
        // that cannot be replaced is the one that fails a copy, and saying so
        // is what turns the badge into an explanation.
        if (info.replaceBlocked && !info.writeBlocked)
            text += " (cannot be replaced)";
        else if (info.writeBlocked && !info.replaceBlocked)
            text += " (cannot be written)";
        return text;
    }

} // namespace UltraCanvas
