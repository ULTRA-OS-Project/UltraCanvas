// include/UltraCanvasCoderModuleRepair.h
// Repairs an installed Windows package in which an ImageMagick coder module
// carries the file name of a Windows system DLL.
//
// Windows keys the modules of a process by base name. ImageMagick loads
// every coder into the process the first time libvips asks it whether it
// recognises a file; with the coder "mpr.dll" (the MPR: in-memory image
// registry) among them, every later import of "MPR.dll" by a Windows
// component the shell loads in here - pcacli.dll for the runas verb,
// daxexec.dll for a Store app - was bound to the coder and failed with
// "The procedure entry point WNetGetConnectionW could not be located".
// package-win.sh no longer ships such a file, but a package extracted over
// an older one keeps the old coders, and so does any 0.9.92-or-older
// extraction. This runs before the image subsystem starts and puts such a
// folder right:
//
//   - a pseudo-format of no use here (mpr, url) is deleted with its .la;
//   - a real format (dpx: SMPTE DPX, which the export dialog offers) is
//     renamed to <name>-coder.dll and its .la rewritten to point there.
//     ImageMagick opens a coder through its .la, whose dlname line names
//     the file to load, so the rename is invisible to it.
//
// The platform-free half is testable anywhere: RepairCoderModules takes the
// folder and the "is this a system DLL's name" answer. On Windows,
// RepairPackagedCoderModules finds the package's coder folders beside the
// executable and asks System32; on every other platform it does nothing.
// Version: 1.0.0
// Last Modified: 2026-10-01
// Author: UltraCanvas Framework
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace UltraCanvas {
    namespace CoderModuleRepair {

        struct CoderModuleRepairResult {
            std::vector<std::string> removed;   // "mpr.dll", "mpr.la"
            std::vector<std::string> renamed;   // "dpx.dll -> dpx-coder.dll"
            std::vector<std::string> failed;    // "mpr.dll: <why>"
            bool Changed() const { return !removed.empty() || !renamed.empty(); }
        };

        // The ASCII lower-case form of a file name, for every comparison here.
        std::string LowerCaseName(const std::string& name);

        // Coder file names (lower case, with ".dll") that are also Windows
        // system DLL names on Windows 10 and 11: mpr, url, dpx, vid. Fixed
        // here so a machine whose System32 lacks one still repairs it.
        bool IsKnownSystemDllName(const std::string& lowerName);

        // The coders that are deleted rather than renamed: pseudo-formats
        // (mpr: the in-memory registry, url: fetch over HTTP) no application
        // here has a use for.
        bool IsDroppableCoder(const std::string& lowerName);

        // "dpx.dll" -> "dpx-coder.dll".
        std::string RenamedCoderFileName(const std::string& dllName);

        // The libtool archive text with its dlname= and library_names= lines
        // naming dlName; every other line as it was.
        std::string RewriteLibtoolArchive(const std::string& laText,
                                          const std::string& dlName);

        // Repairs one coder folder (UTF-8 path). Every *.dll whose lower-case
        // name isSystemDllName answers true for is deleted (droppable) or
        // renamed with its .la rewritten; a renamable coder with no .la is
        // deleted, since nothing could point ImageMagick at the new name.
        // A missing folder is not an error. Idempotent: a repaired folder
        // comes back unchanged.
        CoderModuleRepairResult RepairCoderModules(
                const std::string& codersDirUtf8,
                const std::function<bool(const std::string& lowerName)>& isSystemDllName);

        // Windows: repairs <exeDir>/lib/ImageMagick-*/modules-Q16HDRI/coders
        // against the known names and System32. Call it before the image
        // subsystem starts, i.e. before anything can load a coder. Every
        // other platform returns an empty result.
        CoderModuleRepairResult RepairPackagedCoderModules(const std::string& exeDirUtf8);

        // Platform backend: does the Windows system directory hold a DLL of
        // this (lower-case) name? False where there is no such directory.
        bool NativeIsSystemDllName(const std::string& lowerName);

    } // namespace CoderModuleRepair
} // namespace UltraCanvas
