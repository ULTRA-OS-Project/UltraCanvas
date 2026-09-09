// include/UltraCanvasMacBundle.h
// macOS application bundles and the shortcut files a Mac desktop uses.
//
// The third of the three desktops, after UltraCanvasShellLink (Windows .lnk)
// and UltraCanvasDesktopEntry (freedesktop .desktop). macOS spreads the same
// idea over three things:
//
//   * an application bundle (".app") — a directory the Finder presents as one
//     object, whose Info.plist names it, names its executable and names the
//     ".icns" it is drawn with;
//   * a web location (".webloc") — a property list holding an address;
//   * a Finder alias — a file whose contents are bookmark data, resolvable
//     only by macOS itself (ResolveFinderAlias), and only there.
//
// The first two are read here without any Apple API, so a Mac disk mounted
// on ULTRA OS, Linux or Windows shows its applications with their real names
// and icons, exactly as a Windows disk read from Linux shows its shortcuts.
// Version: 1.0.0
// Last Modified: 2026-09-05
// Author: UltraCanvas Framework
#pragma once

#include <string>

namespace UltraCanvas {

    // ===== AN APPLICATION BUNDLE =====
    struct UCAppBundle {
        std::string bundlePath;    // the ".app" directory itself
        std::string displayName;   // CFBundleDisplayName / CFBundleName, else
                                   // the directory name without ".app"
        std::string identifier;    // CFBundleIdentifier
        std::string version;       // CFBundleShortVersionString
        std::string executable;    // Contents/MacOS/<CFBundleExecutable>, when
                                   // that file is really there
        std::string iconFile;      // the ".icns" it is drawn with, when found
        bool isApplication = false;  // CFBundlePackageType is APPL, or it has
                                     // an executable where one belongs
    };

    // Extension test only, no file access: does this path name a bundle?
    // True for the package kinds the Finder presents as one object rather
    // than as a folder (".app", ".framework", ".bundle", ".prefPane", …).
    bool IsBundlePath(const std::string& path);
    // The narrower question, and the one activation cares about.
    bool IsApplicationBundlePath(const std::string& path);

    // Read the bundle's Info.plist. False when `path` is not a bundle
    // directory or carries no readable Info.plist — so a folder that merely
    // ends in ".app" is never mistaken for an application.
    bool ReadApplicationBundle(const std::string& path, UCAppBundle& out);

    // ===== A WEB LOCATION (".webloc") =====
    bool IsWebLocationPath(const std::string& path);
    // The address inside it (the plist's "URL" key). False when the file is
    // not one, or holds no address.
    bool ReadWebLocation(const std::string& path, std::string& outUrl);

    // ===== A FINDER ALIAS =====
    // Cheap check: does this file begin with bookmark data? Alias files are
    // ordinary files whose contents start with one of two magic words, which
    // is as much as can be said about one without asking macOS.
    bool IsFinderAliasFile(const std::string& path);
    // Resolve one to the file it points at. Implemented on macOS (through
    // the bookmark-resolving API, which is the only thing that can follow a
    // file that has since moved); false everywhere else, where an alias is
    // just a file nothing can follow.
    bool ResolveFinderAlias(const std::string& path, std::string& outTarget);

} // namespace UltraCanvas
