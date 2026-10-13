// Apps/UltraPress/engine/UltraPressMedia.cpp
// See UltraPressMedia.h.
// Version: 0.6.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressMedia.h"

#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>

namespace fs = std::filesystem;
using UltraCanvas::JSONValue;

namespace UltraPress {

namespace {

std::string LowerExtension(const std::string& name) {
    const size_t dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0) return "";
    std::string ext = name.substr(dot + 1);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

bool IsAbsoluteUrl(const std::string& path) {
    return path.find("://") != std::string::npos || path.rfind("data:", 0) == 0 || path.rfind("mailto:", 0) == 0;
}

std::string RelativeUnder(const fs::path& root, const fs::path& file) {
    std::string relative = UltraCanvas::PathToUtf8(file.lexically_relative(root));
    std::replace(relative.begin(), relative.end(), '\\', '/');
    return relative;
}

void CollectBlock(const Block& block, const std::string& page, const std::string& at, const BlockRegistry& registry,
                  std::vector<MediaReference>& out) {
    if (const BlockType* type = registry.Find(block.type)) {
        for (const BlockProperty& property : type->properties) {
            if (property.kind == PropertyKind::Image) {
                const std::string file = block.Text(property.name);
                if (!file.empty() && !IsAbsoluteUrl(file)) out.push_back({page, at + "." + property.name, file});
            } else if (property.kind == PropertyKind::Items) {
                const JSONValue& items = block.Items(property.name);
                size_t index = 0;
                for (const JSONValue& item : items.GetElements()) {
                    const std::string file = item["file"].GetString();
                    if (!file.empty() && !IsAbsoluteUrl(file)) {
                        out.push_back({page, at + "." + property.name + "[" + std::to_string(index) + "].file", file});
                    }
                    ++index;
                }
            }
        }
    }
    for (size_t i = 0; i < block.children.size(); ++i) {
        CollectBlock(block.children[i], page, at + ".children[" + std::to_string(i) + "]", registry, out);
    }
}

} // namespace

std::string MediaKind(const std::string& name) {
    static const std::map<std::string, std::string> kKinds = {
        {"png", "image"}, {"jpg", "image"}, {"jpeg", "image"}, {"gif", "image"}, {"webp", "image"}, {"svg", "image"},
        {"avif", "image"}, {"bmp", "image"}, {"tif", "image"}, {"tiff", "image"}, {"ico", "image"},
        {"pdf", "document"}, {"txt", "document"}, {"md", "document"}, {"doc", "document"}, {"docx", "document"},
        {"odt", "document"}, {"xls", "document"}, {"xlsx", "document"}, {"ods", "document"}, {"ppt", "document"},
        {"pptx", "document"}, {"odp", "document"}, {"epub", "document"},
        {"mp4", "video"}, {"webm", "video"}, {"mov", "video"}, {"mkv", "video"},
        {"mp3", "audio"}, {"ogg", "audio"}, {"oga", "audio"}, {"wav", "audio"}, {"flac", "audio"}, {"m4a", "audio"},
        {"zip", "archive"}, {"7z", "archive"}, {"tar", "archive"}, {"gz", "archive"}, {"xz", "archive"}, {"dmg", "archive"},
        {"exe", "archive"}, {"msi", "archive"}, {"appimage", "archive"}, {"deb", "archive"}, {"rpm", "archive"},
    };
    auto it = kKinds.find(LowerExtension(name));
    return it == kKinds.end() ? "other" : it->second;
}

std::string HumanSize(int64_t bytes) {
    char out[32];
    if (bytes < 1024) std::snprintf(out, sizeof out, "%lld B", static_cast<long long>(bytes));
    else if (bytes < 1024 * 1024) std::snprintf(out, sizeof out, "%lld KB", static_cast<long long>((bytes + 512) / 1024));
    else if (bytes < 100LL * 1024 * 1024) std::snprintf(out, sizeof out, "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));   // locale-ok: "%.1f" is formatted by snprintf in the C locale UltraCanvas keeps
    else std::snprintf(out, sizeof out, "%lld MB", static_cast<long long>(bytes / (1024 * 1024)));
    return out;
}

std::vector<MediaReference> MediaReferences(const Site& site, const BlockRegistry& registry) {
    std::vector<MediaReference> out;
    for (const Page& page : site.pages) {
        if (!page.seo.image.empty() && !IsAbsoluteUrl(page.seo.image)) out.push_back({page.slug, "seo.image", page.seo.image});
        for (size_t i = 0; i < page.blocks.size(); ++i) {
            CollectBlock(page.blocks[i], page.slug, "blocks[" + std::to_string(i) + "]", registry, out);
        }
    }
    return out;
}

std::vector<MediaFile> ListMedia(const std::string& siteDirectory, const Site* site, const BlockRegistry& registry) {
    std::vector<MediaFile> out;
    const fs::path root = UltraCanvas::PathFromUtf8(siteDirectory);
    const fs::path media = root / "media";
    std::error_code ec;
    if (!fs::is_directory(media, ec)) return out;
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(media, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        MediaFile file;
        file.path = RelativeUnder(root, entry.path());
        file.name = UltraCanvas::PathToUtf8(entry.path().filename());
        file.kind = MediaKind(file.name);
        file.size = static_cast<int64_t>(entry.file_size(ec));
        out.push_back(std::move(file));
    }
    std::sort(out.begin(), out.end(), [](const MediaFile& a, const MediaFile& b) { return a.path < b.path; });
    if (site) {
        std::map<std::string, int> uses;
        for (const MediaReference& reference : MediaReferences(*site, registry)) ++uses[reference.file];
        for (MediaFile& file : out) {
            auto it = uses.find(file.path);
            file.usedBy = it == uses.end() ? 0 : it->second;
        }
    }
    return out;
}

std::vector<ValidationIssue> ValidateMedia(const Site& site, const std::string& siteDirectory, const BlockRegistry& registry) {
    std::vector<ValidationIssue> issues;
    const fs::path root = UltraCanvas::PathFromUtf8(siteDirectory);
    std::error_code ec;
    for (const MediaReference& reference : MediaReferences(site, registry)) {
        if (reference.file.rfind("media/", 0) != 0 && reference.file[0] != '/') {
            issues.push_back({reference.where, reference.at, "names a file outside media/: '" + reference.file + "'"});
            continue;
        }
        const std::string relative = reference.file[0] == '/' ? reference.file.substr(1) : reference.file;
        if (!fs::is_regular_file(root / UltraCanvas::PathFromUtf8(relative), ec)) {
            issues.push_back({reference.where, reference.at, "names a file the site does not have: '" + reference.file + "'"});
        }
    }
    return issues;
}

bool ImportMedia(const std::string& siteDirectory, const std::string& sourcePath, std::string& mediaPath, std::string& error) {
    const fs::path root = UltraCanvas::PathFromUtf8(siteDirectory);
    const fs::path source = UltraCanvas::PathFromUtf8(sourcePath);
    std::error_code ec;
    if (!fs::is_regular_file(source, ec)) {
        error = sourcePath + ": not a file";
        return false;
    }
    const fs::path media = root / "media";
    // Already one of ours: a file under media/ is named, not copied twice.
    const fs::path canonicalSource = fs::weakly_canonical(source, ec);
    const fs::path canonicalMedia = fs::weakly_canonical(media, ec);
    if (!canonicalSource.empty() && !canonicalMedia.empty()) {
        const std::string inside = UltraCanvas::PathToUtf8(canonicalSource.lexically_relative(canonicalMedia));
        if (!inside.empty() && inside.rfind("..", 0) != 0 && inside != ".") {
            mediaPath = "media/" + inside;
            std::replace(mediaPath.begin(), mediaPath.end(), '\\', '/');
            return true;
        }
    }
    fs::create_directories(media, ec);
    if (ec) {
        error = "cannot create " + UltraCanvas::PathToUtf8(media) + ": " + ec.message();
        return false;
    }
    const std::string name = UltraCanvas::PathToUtf8(source.filename());
    const std::string ext = LowerExtension(name);
    const std::string stem = name.substr(0, ext.empty() ? name.size() : name.size() - ext.size() - 1);
    std::string base = MakeSlug(stem);
    if (base == "page") base = "file";   // MakeSlug's fallback for an empty name
    std::string candidate = base + (ext.empty() ? "" : "." + ext);
    for (int n = 2; fs::exists(media / UltraCanvas::PathFromUtf8(candidate), ec); ++n) {
        candidate = base + "-" + std::to_string(n) + (ext.empty() ? "" : "." + ext);
    }
    const fs::path target = media / UltraCanvas::PathFromUtf8(candidate);
    fs::copy_file(source, target, fs::copy_options::none, ec);
    if (ec) {
        error = "cannot copy " + sourcePath + ": " + ec.message();
        return false;
    }
    mediaPath = "media/" + candidate;
    return true;
}

bool RemoveMedia(const std::string& siteDirectory, const std::string& mediaPath, std::string& error, const Site* site,
                 const BlockRegistry& registry) {
    if (mediaPath.rfind("media/", 0) != 0 || mediaPath.find("..") != std::string::npos) {
        error = mediaPath + ": not a file of the library";
        return false;
    }
    if (site) {
        std::vector<std::string> where;
        for (const MediaReference& reference : MediaReferences(*site, registry)) {
            if (reference.file == mediaPath) where.push_back((reference.where.empty() ? "site" : reference.where) + " " + reference.at);
        }
        if (!where.empty()) {
            error = mediaPath + " is used by " + where.front() + (where.size() > 1 ? " and " + std::to_string(where.size() - 1) + " more" : "");
            return false;
        }
    }
    const fs::path file = UltraCanvas::PathFromUtf8(siteDirectory) / UltraCanvas::PathFromUtf8(mediaPath);
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        error = mediaPath + ": no such file";
        return false;
    }
    if (!fs::remove(file, ec)) {
        error = mediaPath + ": cannot remove: " + ec.message();
        return false;
    }
    // The folders left empty with it, up to media/ itself (kept).
    for (fs::path folder = file.parent_path(); folder != UltraCanvas::PathFromUtf8(siteDirectory) / "media" && fs::is_empty(folder, ec);
         folder = folder.parent_path()) {
        fs::remove(folder, ec);
    }
    return true;
}

} // namespace UltraPress
