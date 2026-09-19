// OS/Linux/UltraCanvasLinuxHostFileIcons.cpp
// The freedesktop backend of UltraCanvasHostFileIcons: the icon this desktop
// draws for a file of a given type, which on Linux/BSD is a two-step lookup
// and no shell call at all.
//
//   1. the file's type, as a MIME name — UltraCanvasFileAssociations, which
//      already keeps the shared-mime-info globs the "Open with" menu is built
//      from, so there is exactly one reader of that database;
//   2. the icon NAMES that type is drawn under, per the icon-naming
//      specification ("text/plain" is "text-plain", falling back to the
//      "text-x-generic" every theme ships), looked up in the installed themes
//      by UltraCanvasDesktopEntry, which already resolves an Icon= the same
//      way for desktop entries.
//
// So this file is the naming rules and nothing else: both halves of the work
// are somebody's existing job. A machine with no icon theme installed answers
// nothing, and the file display keeps its own drawn icons.
// Version: 1.0.0
// Last Modified: 2026-09-17
// Author: UltraCanvas Framework
#include "UltraCanvasHostFileIcons.h"
#include "UltraCanvasDesktopEntry.h"
#include "UltraCanvasFileAssociations.h"
#include "UltraCanvasIconResource.h"

#include <algorithm>
#include <string>
#include <vector>

namespace UltraCanvas {

    bool HostFileIconsAvailable() { return true; }

    namespace {

        // The smallest and largest a theme lookup is worth asking for: below
        // the first the icon is a smudge, above the last every theme answers
        // with its scalable icon anyway.
        constexpr int kMinIconEdge = 8;
        constexpr int kMaxIconEdge = 512;

        // "text/plain" → "text-plain". The icon-naming specification's rule
        // for a MIME icon, and what every theme is indexed by.
        std::string MimeIconName(const std::string& mime) {
            std::string name = mime;
            std::replace(name.begin(), name.end(), '/', '-');
            return name;
        }

        // The icon names to try, most specific first. A theme is free to
        // carry none of them, which is why there is a chain rather than a
        // name: a plain text file is "text-plain" in a complete theme,
        // "text-x-generic" in a small one, and nothing at all in a theme that
        // only dresses the window buttons.
        std::vector<std::string> IconNamesFor(const std::string& path,
                                              bool isDirectory) {
            if (isDirectory) return {"inode-directory", "folder"};

            std::vector<std::string> names;
            const std::string mime = FileAssociations::GetMimeType(path);
            if (!mime.empty()) {
                names.push_back(MimeIconName(mime));
                // The generic icon the type database names for this type.
                // This is what makes a PDF an office document and a tarball a
                // package rather than both "some file": no theme ships an
                // icon for every one of the two thousand known types, and the
                // database says which picture each of them belongs under.
                const std::string generic =
                        FileAssociations::GetMimeGenericIcon(mime);
                if (!generic.empty()) names.push_back(generic);
                // The generic icon of the media type, which is the rule the
                // database lists only the exceptions to: what a theme draws
                // for every kind of text, picture, sound or video it has no
                // specific icon for.
                const size_t slash = mime.find('/');
                if (slash != std::string::npos && slash > 0)
                    names.push_back(mime.substr(0, slash) + "-x-generic");
            }
            // Last resorts, in the order a desktop falls back itself: the
            // icon for "some file", then the question-mark one.
            names.push_back("application-x-generic");
            names.push_back("unknown");
            return names;
        }

        // An icon file the themes named, at the size asked for. Themes are
        // mostly PNG and SVG, both of which the image pipeline reads; they do
        // also ship Windows .ico files, which it does not, so those go
        // through the icon-resource reader first - the same order the native
        // file-icon module uses for a desktop entry's icon.
        std::shared_ptr<UCPixmap> PixmapFromIconFile(const std::string& file,
                                                     int edge) {
            if (file.empty()) return nullptr;
            if (HasIconResourceExtension(file)) {
                if (auto pixmap = LoadIconResource(file, 0, edge)) return pixmap;
            }
            auto image = UCImage::Get(file);
            if (!image || image->GetWidth() <= 0 || image->GetHeight() <= 0)
                return nullptr;
            return image->GetPixmap(edge, edge, ImageFitMode::Contain, 1.0f);
        }

    } // namespace

    std::shared_ptr<UCPixmap> LoadHostFileIconPixmap(const std::string& path,
                                                     bool isDirectory,
                                                     int desiredSize) {
        if (path.empty()) return nullptr;
        const int edge = std::max(kMinIconEdge,
                                  std::min(desiredSize, kMaxIconEdge));
        for (const std::string& name : IconNamesFor(path, isDirectory)) {
            const std::string file = FindDesktopIconFile(name, edge);
            if (file.empty()) continue;
            if (auto pixmap = PixmapFromIconFile(file, edge)) return pixmap;
        }
        return nullptr;
    }

    void RefreshHostFileIcons() {
        // The theme, its inheritance chain and every name already resolved -
        // which is the whole of what this backend remembers.
        RefreshDesktopIconThemes();
    }

} // namespace UltraCanvas
