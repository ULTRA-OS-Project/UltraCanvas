// Apps/UltraPress/engine/UltraPressMedia.h
// The media library: the files under <site>/media - pictures, downloads -
// listed with what uses them, brought in from anywhere on the disk under a
// clean name, removed when nothing needs them, and every reference a page
// makes to one checked against the folder. Headless; the editor's library
// dialog and `ultrapress media` are two faces of it.
// Version: 0.7.1 - ImageDimensions from the file's header; PruneMedia; a clean name for an imported file
// Version: 0.6.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraPressBlocks.h"
#include "UltraPressModel.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace UltraPress {

// One file of the library. `path` is what a block names: relative to the
// site folder, '/'-separated, "media/2026/launch.png".
struct MediaFile {
    std::string path;
    std::string name;         // the file name alone
    std::string kind;         // "image", "document", "video", "audio", "archive", "other"
    int64_t size = 0;
    int width = 0, height = 0;   // a picture's pixels, from its header; 0 when unknown
    int usedBy = 0;           // references from the site (0 when no site was given)
};

// Where a page names a media file: `where` is the page's slug ("" for the
// site), `at` the property path ("blocks[2].picture", "seo.image",
// "blocks[1].items[0].file"), `file` the path as written.
struct MediaReference {
    std::string where;
    std::string at;
    std::string file;
};

// The kind a file name's extension gives it.
std::string MediaKind(const std::string& name);

// A picture's pixel size from its header - PNG, JPEG, GIF, WebP, BMP -
// without decoding it, so a list of a thousand pictures costs a thousand
// small reads. False for anything else, or a file that cannot be read.
bool ImageDimensions(const std::string& path, int& width, int& height);

// The name a file gets in the library: its stem slugged, the extension
// lower-case; "-2", "-3" appended while `taken` says the name is in use.
// "My Launch Photo.PNG" is "my-launch-photo.png".
std::string CleanMediaName(const std::string& fileName, const std::function<bool(const std::string&)>& taken);

// Removes every file no page uses; what went is listed. False when one
// could not be removed (listed in `errors`); nothing to remove is true.
bool PruneMedia(const std::string& siteDirectory, const Site& site, std::vector<std::string>& removed,
                std::vector<std::string>& errors, const BlockRegistry& registry = BlockRegistry::BuiltIn());

// Every file under <site>/media, recursively, in path order. With a site,
// `usedBy` counts its references.
std::vector<MediaFile> ListMedia(const std::string& siteDirectory, const Site* site = nullptr,
                                 const BlockRegistry& registry = BlockRegistry::BuiltIn());

// Every media path the site names: the Image properties of every block (and
// of the children), the `file` of a downloads item, each page's social card
// picture. Absolute URLs are not media and are left out.
std::vector<MediaReference> MediaReferences(const Site& site, const BlockRegistry& registry = BlockRegistry::BuiltIn());

// References to files that are not in the folder, or not under media/.
std::vector<ValidationIssue> ValidateMedia(const Site& site, const std::string& siteDirectory,
                                           const BlockRegistry& registry = BlockRegistry::BuiltIn());

// Copies `sourcePath` into <site>/media under a clean name - the file name
// slugged, the extension lower-case, "-2", "-3" when the name is taken -
// and returns the media path in `mediaPath`. A source that is already a
// file of the library is returned as it is, not copied.
bool ImportMedia(const std::string& siteDirectory, const std::string& sourcePath, std::string& mediaPath,
                 std::string& error);

// Removes a file of the library. Refuses a path outside media/, and - with a
// site - one a page still uses, naming where.
bool RemoveMedia(const std::string& siteDirectory, const std::string& mediaPath, std::string& error,
                 const Site* site = nullptr, const BlockRegistry& registry = BlockRegistry::BuiltIn());

// A size for people: "12 KB", "3.4 MB".
std::string HumanSize(int64_t bytes);

} // namespace UltraPress
