// Apps/UltraFiler/UltraFilerRamDisks.h
// "+ Drive > RAM disc...": the RAM discs UltraFiler creates, lists and ejects.
//
// A thin layer over VirtualFS's RAM disc API (VirtualFS/VirtualFSRamDisk.h),
// so the window never includes VirtualFS itself and a build without the
// VirtualFS module still compiles - Available() is then false and the menu
// item says why. A disc is a real mounted path (a directory on the /dev/shm
// tmpfs on Linux, an hdiutil ram:// volume on macOS, an ImDisk drive on
// Windows), so everything below it is browsed as ordinary local files.
//
// A disc outlives the window: it stays until the user ejects it (or the
// machine restarts), and List() finds it again when UltraFiler starts.
// Version: 1.0.1 - MaxNameLength
// Last Modified: 2026-10-05
// Author: UltraCanvas Framework
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace UltraFilerRamDisks {

struct RamDisc {
    std::string name;        // the label the user gave it
    std::string mountPath;   // where its files are - a real local path
    // Size of the disc; 0 when the platform cannot say. On Linux the discs
    // share /dev/shm and have no size of their own, so this is 0 there.
    uint64_t sizeBytes = 0;
    // False for the Windows fallback, which is ordinary storage wiped on
    // eject - never show it as memory.
    bool trueRam = false;
};

// Whether this build can make RAM discs at all (it was built with VirtualFS,
// and the platform has a way to make one).
bool Available();

// Whether a disc made now would really live in memory. False on Windows
// without ImDisk installed, where the disc is a folder in %TEMP% instead.
bool TrueRamAvailable();

// Whether a disc's size is enforced. False on Linux: every disc is a
// directory on the one /dev/shm tmpfs, which grows with what is written, up
// to that mount's limit shared by all of them.
bool SizeIsEnforced();

// Most a new disc can hold: the memory free right now, or on Linux the space
// free on /dev/shm when that is less. 0 when it cannot be read.
uint64_t LargestPossibleBytes();

// The discs of this user that are mounted now, including ones an earlier
// UltraFiler (or another program) made.
std::vector<RamDisc> List();

// A name no mounted disc uses yet: "RAM1", "RAM2", ...
std::string SuggestName();

// The longest name a disc can have on this system: 64, and 23 on Windows,
// where an ImDisk disc keeps its name in its NTFS volume label
// (VirtualFS_GetMaxRamDiskNameLength).
std::size_t MaxNameLength();

// Whether `name` can name a disc: 1 to MaxNameLength() of A-Z a-z 0-9 . _ -
// (it becomes part of a path).
bool IsValidName(const std::string& name);

// Makes a disc. Fills `outDisc` and returns true, or returns false with a
// message fit for an alert in `outError`.
bool Create(const std::string& name, uint64_t sizeBytes, RamDisc& outDisc,
            std::string& outError);

// Unmounts the disc mounted at `mountPath`. Everything on it is gone.
bool Eject(const std::string& mountPath, std::string& outError);

// "512 MB", "2 GB", "1.5 GB" - binary units, which is what memory is sold in.
std::string FormatSize(uint64_t bytes);

} // namespace UltraFilerRamDisks
} // namespace UltraCanvas
