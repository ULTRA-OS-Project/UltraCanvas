// include/UltraCanvasFileLock.h
// Answers "is this file in use by another program right now" - the question
// behind every "the action can't be completed because the file is open in
// another program" a file manager runs into.
//
//   FileLockInfo info = ProbeFileLock(path);
//   if (info.state == FileLockState::Locked) { /* cannot be replaced now */ }
//
// The probe NEVER modifies the file and never takes a lock of its own: it
// asks the system for the access an overwrite would need, with every sharing
// flag granted on our side, and closes it again. A probe is therefore never
// what another program trips over.
//
// The answer means different things per platform, and the state says which:
//
//   Locked         The system refuses the write or the replace right now:
//                  Windows sharing (a running .exe, a document a program
//                  holds), or a POSIX write lock held by another process.
//   OpenElsewhere  Another process has the file open, but that does not stop
//                  it being written or replaced. This is the normal POSIX
//                  case, and it is information, not a warning.
//   Free           Nothing is in the way.
//   Unknown        Not probed, no backend on this platform, or the file
//                  disappeared under the probe.
//
// Backends: Windows (share-mode probe + Restart Manager for the holder names)
// and Linux (fcntl locks + /proc). Where none exists - macOS, Android,
// WebAssembly - every probe answers Unknown and FileLockProbeAvailable() is
// false, so a caller can leave the display out entirely.
// Version: 1.0.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework
#pragma once

#include <string>
#include <vector>

namespace UltraCanvas {

    // ===== WHAT THE PROBE FOUND =====
    enum class FileLockState {
        Unknown = 0,    // no answer (no backend, gone, not probed)
        Free,           // nothing holds it
        OpenElsewhere,  // another process has it open, writing still works
        Locked          // a write or a replace would fail right now
    };

    struct FileLockInfo {
        FileLockState state = FileLockState::Unknown;
        // Which access the system refused. Both false for Free / Unknown, and
        // for OpenElsewhere - where the file is open but nothing is blocked.
        bool writeBlocked = false;    // cannot be opened for writing
        bool replaceBlocked = false;  // cannot be renamed / replaced / deleted
        // Programs holding the file, when the platform can name them, as
        // "Firefox (1234)". Empty otherwise - including on a platform that
        // knows the file is in use but not by whom. Only filled when the
        // probe was asked for it: naming the holder costs far more than
        // answering whether there is one.
        std::vector<std::string> holders;

        bool Blocks() const { return state == FileLockState::Locked; }
        bool InUse() const {
            return state == FileLockState::Locked ||
                   state == FileLockState::OpenElsewhere;
        }
    };

    // ===== BACKEND INTERFACE (implemented per platform under OS/<Platform>/) =====
    // Not part of the public surface: callers use the functions below. `out`
    // is resized to `paths.size()`; entries the backend cannot answer stay
    // Unknown. False means this platform has no backend at all.
    bool NativeProbeFileLocks(const std::vector<std::string>& paths,
                              bool wantHolders,
                              std::vector<FileLockInfo>& out);

    // ===== PROBES =====
    // One file. `wantHolders` also asks WHO holds it, which is a much more
    // expensive question - ask it for the one file a user is looking at, not
    // for a listing.
    FileLockInfo ProbeFileLock(const std::string& path, bool wantHolders = false);

    // A batch, in one pass. Prefer this for a folder listing: on Linux the
    // open-file information is system-wide, so one call walks /proc once
    // instead of once per file. The result has one entry per input path, in
    // the same order.
    std::vector<FileLockInfo> ProbeFileLocks(const std::vector<std::string>& paths,
                                             bool wantHolders = false);

    // Whether this build can answer at all. False means every probe returns
    // Unknown, so a caller can skip both the call and the column.
    bool FileLockProbeAvailable();

    // ===== PRESENTATION =====
    // The attribute letter for a compact display ("DRH" style): 'X' for
    // Locked, 'O' for OpenElsewhere, 0 for a state worth no letter.
    char FileLockAttributeLetter(FileLockState state);

    // One human sentence, or "" for Free / Unknown: "In use by Firefox (1234)",
    // "In use by another program (cannot be replaced)", "Open in another
    // program".
    std::string FileLockText(const FileLockInfo& info);

} // namespace UltraCanvas
