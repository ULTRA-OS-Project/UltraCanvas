// core/UltraCanvasNativeFileIcons.cpp
// The portable half of the native file-icon service: the icon a Windows
// program, icon file or shortcut carries, read from the file itself
// (UltraCanvasIconResource / UltraCanvasShellLink) rather than from a shell.
// This is what ULTRA OS, Linux and macOS use - a Windows disk mounted there,
// or the drive_c of a Wine prefix, holds the same .exe and .lnk files as it
// does on Windows, and their icons are in them. Windows itself uses the
// shell instead (OS/MSWindows/UltraCanvasWindowsFileIcons.cpp), which also
// covers the file types only a registry association can answer for.
// Version: 1.2.0
// Last Modified: 2026-09-05
// Author: UltraCanvas Framework
#include "UltraCanvasNativeFileIcons.h"
#include "UltraCanvasIconResource.h"
#include "UltraCanvasShellLink.h"

#ifndef _WIN32
namespace UltraCanvas {

    bool NativeFileIconAvailable(const std::string& path) {
        return HasIconResourceExtension(path) || IsShellLinkPath(path);
    }

    std::shared_ptr<UCPixmap> LoadNativeFileIconPixmap(const std::string& path,
                                                       int desiredSize) {
        if (IsShellLinkPath(path)) {
            // A shortcut is drawn with the icon it names - which is a file
            // somewhere else entirely, usually the program it starts.
            UCShellLink link;
            if (!ReadShellLink(path, link)) return nullptr;
            if (!link.hostIconLocation.empty()) {
                if (auto pixmap = LoadIconResource(link.hostIconLocation,
                                                   link.iconIndex, desiredSize))
                    return pixmap;
            }
            // No icon of its own, or one that has gone missing or holds no
            // icon resource: what a shortcut shows then is the icon of what
            // it points at.
            if (!link.hostTargetPath.empty() &&
                link.hostTargetPath != link.hostIconLocation)
                return LoadIconResource(link.hostTargetPath, 0, desiredSize);
            return nullptr;
        }
        if (auto pixmap = LoadIconResource(path, 0, desiredSize)) return pixmap;
        // An icon file this reader cannot decode - one holding a frame in a
        // format only the image pipeline knows - is still an image, so it
        // gets one more chance through the normal decoder before the file
        // display falls back to a type glyph.
        const size_t dot = path.find_last_of('.');
        if (dot != std::string::npos && path.size() - dot == 4 &&
            (path[dot + 1] == 'i' || path[dot + 1] == 'I') &&
            (path[dot + 2] == 'c' || path[dot + 2] == 'C') &&
            (path[dot + 3] == 'o' || path[dot + 3] == 'O')) {
            auto image = UCImage::Get(path);
            if (image && image->GetWidth() > 0 && image->GetHeight() > 0) {
                return image->GetPixmap(desiredSize, desiredSize,
                                        ImageFitMode::Contain, 1.0f);
            }
        }
        return nullptr;
    }

    // Reading a file needs no per-thread setup; the scope exists for the
    // Windows shell, which does.
    NativeFileIconThreadScope::NativeFileIconThreadScope() = default;
    NativeFileIconThreadScope::~NativeFileIconThreadScope() = default;

} // namespace UltraCanvas
#endif // !_WIN32
