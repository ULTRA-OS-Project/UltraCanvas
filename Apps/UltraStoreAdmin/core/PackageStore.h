// Apps/UltraStoreAdmin/core/PackageStore.h
// The files behind the packages: a build the admin picks is copied into the
// package store under its hash (`<store>/packages/<sha256>.<format>`), so
// two releases that ship the same bytes share one file and the catalog's
// hash and size are measured, never typed. The entry an app is started by
// is suggested from the archive's listing (VirtualFS) and confirmed by the
// admin.
// Version: 0.3.0 - ImportMediaVariants: the picture at the web store's sizes in AVIF and WebP, with the budgets
// Version: 0.2.0 - ImportMedia, AdminStore, DefaultAdminStoreDir (shared by the commands and the window)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "Catalog.h"
#include "UltraStoreTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraStoreAdmin {

using UltraStore::ErrorCode;
using UltraStore::Result;

struct ImportedPackage {
    std::string file;      // UTF-8, in the package store
    std::string sha256;
    uint64_t size = 0;
    std::string format;    // "zip", "7z", "tar.zst", "tar.gz", "tar.xz", "appimage"
};

// The catalog's format name for a file, from its extension; "" for one the
// store cannot install.
std::string FormatOf(const std::string& fileName);

// Copies the file into the package store (created when missing) under its
// hash. A file already there with the same hash is reused, not rewritten.
Result<ImportedPackage> ImportPackage(const std::string& sourceUtf8, const std::string& packagesDirUtf8);

// The archive's entries (VirtualFS); NotAvailable when this build has no
// archive provider, IoError when the file does not list.
Result<std::vector<std::string>> ListPackageEntries(const std::string& fileUtf8);

// A picture into the media store: `<store>/media/<component id>/<file name>`,
// overwritten when it is there; returns the path the catalog carries,
// "media/<id>/<file>", relative to the store folder here and to the
// catalog's URL once published.
Result<std::string> ImportMedia(const std::string& sourceUtf8, const std::string& storeDirUtf8, const std::string& componentId);

// ----- the picture variants (Docs/UltraStore/WebstoreBrief.md, *The picture rules*) -----
// The web store never resizes a picture in the browser: the sizes it shows
// are produced here, once, beside the original, and the catalog names them
// (Component::iconVariants, screenshotVariants). An icon at 64, 128 and
// 256 px (the card, the app page, the hero at 2x); a screenshot at 560 and
// 1120 px wide (the design's column at 1x and 2x), never above the
// original's width - a smaller original gives the sizes it has. Each size
// as AVIF (quality 65) and WebP (lossless for an icon, quality 80 for a
// screenshot), metadata stripped, a screenshot without alpha. The files are
// `<stem>-<width>.avif` / `.webp` in the component's media folder.
enum class MediaKind { Icon, Screenshot };

struct ImportedMedia {
    std::string original;                            // "media/<id>/<file>", what icon / screenshots carry
    std::vector<UltraStore::MediaVariant> variants;  // what was written, smallest width first, AVIF before WebP
    std::vector<std::string> warnings;               // a format this machine cannot write, a file over its budget
};

// The sizes of a kind, ascending.
const std::vector<int>& MediaWidths(MediaKind kind);

// What a published file of the kind and width may weigh, in bytes: an icon
// 12 KB at 256 px, a screenshot 120 KB at 1120 px, scaled down with the
// width (the budget is per pixel), never under 2 KB (a container's own
// bytes). ImportMediaVariants and publish warn above it.
size_t MediaBudget(MediaKind kind, int width);

// ImportMedia, then the variants beside the original. A format the image
// library cannot write here (no AVIF encoder, say) is a warning, not a
// failure: the catalog names what was written, the original stays the
// fallback. A picture that does not decode (an SVG without the Vector
// plugin) gives the original alone, with a warning.
Result<ImportedMedia> ImportMediaVariants(const std::string& sourceUtf8, const std::string& storeDirUtf8,
                                          const std::string& componentId, MediaKind kind);

// The admin store folder: the database, the package store, the media store.
struct AdminStore {
    std::string dir;   // UTF-8
    std::string DatabasePath() const;
    std::string PackagesDir() const;
    std::string MediaDir() const;
    bool Exists() const;                // the database file is there
    Result<void> CreateFolders() const; // the folder, packages/ and media/
};

// UltraCanvasDataFolderRoot()/UltraStoreAdmin.
std::string DefaultAdminStoreDir();

// The entry to start, guessed from the listing: on Windows the .exe at the
// top or under bin/, on macOS the .app bundle, elsewhere a file under bin/
// without an extension, else the one AppImage; "" when nothing fits.
std::string SuggestEntry(const std::vector<std::string>& entries, UltraStore::OS os);

} // namespace UltraStoreAdmin
