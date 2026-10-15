// Apps/UltraPress/engine/UltraPressMedia.cpp
// See UltraPressMedia.h.
// Version: 0.7.1 - dimensions, prune, clean names
// Version: 0.6.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressMedia.h"

#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
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

namespace {

uint32_t BigEndian32(const unsigned char* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
uint32_t LittleEndian32(const unsigned char* p) { return (uint32_t(p[3]) << 24) | (uint32_t(p[2]) << 16) | (uint32_t(p[1]) << 8) | p[0]; }
uint16_t BigEndian16(const unsigned char* p) { return uint16_t((p[0] << 8) | p[1]); }
uint16_t LittleEndian16(const unsigned char* p) { return uint16_t((p[1] << 8) | p[0]); }

} // namespace

bool ImageDimensions(const std::string& path, int& width, int& height) {
    width = height = 0;
    std::ifstream in(UltraCanvas::PathFromUtf8(path), std::ios::binary);
    if (!in) return false;
    unsigned char head[32] = {};
    in.read(reinterpret_cast<char*>(head), sizeof head);
    const std::streamsize got = in.gcount();
    if (got < 10) return false;
    // PNG: the IHDR chunk follows the 8-byte signature; width and height are big-endian.
    if (got >= 24 && head[0] == 0x89 && head[1] == 'P' && head[2] == 'N' && head[3] == 'G' && std::memcmp(head + 12, "IHDR", 4) == 0) {
        width = static_cast<int>(BigEndian32(head + 16));
        height = static_cast<int>(BigEndian32(head + 20));
        return width > 0 && height > 0;
    }
    // GIF: the logical screen size, little-endian, after "GIF87a"/"GIF89a".
    if (head[0] == 'G' && head[1] == 'I' && head[2] == 'F') {
        width = LittleEndian16(head + 6);
        height = LittleEndian16(head + 8);
        return width > 0 && height > 0;
    }
    // BMP: the info header's width and height (signed; a negative height is top-down).
    if (got >= 26 && head[0] == 'B' && head[1] == 'M') {
        const int32_t w = static_cast<int32_t>(LittleEndian32(head + 18));
        const int32_t h = static_cast<int32_t>(LittleEndian32(head + 22));
        width = w;
        height = h < 0 ? -h : h;
        return width > 0 && height > 0;
    }
    // WebP: "RIFF....WEBP" then a VP8 / VP8L / VP8X chunk.
    if (got >= 30 && std::memcmp(head, "RIFF", 4) == 0 && std::memcmp(head + 8, "WEBP", 4) == 0) {
        if (std::memcmp(head + 12, "VP8X", 4) == 0) {
            width = 1 + (head[24] | (head[25] << 8) | (head[26] << 16));
            height = 1 + (head[27] | (head[28] << 8) | (head[29] << 16));
        } else if (std::memcmp(head + 12, "VP8L", 4) == 0) {
            const uint32_t bits = LittleEndian32(head + 21);
            width = static_cast<int>((bits & 0x3FFF) + 1);
            height = static_cast<int>(((bits >> 14) & 0x3FFF) + 1);
        } else if (std::memcmp(head + 12, "VP8 ", 4) == 0) {
            width = LittleEndian16(head + 26) & 0x3FFF;
            height = LittleEndian16(head + 28) & 0x3FFF;
        }
        return width > 0 && height > 0;
    }
    // JPEG: walk the markers to the first start-of-frame; its height and width are big-endian.
    if (head[0] == 0xFF && head[1] == 0xD8) {
        in.clear();
        in.seekg(2);
        unsigned char marker[4];
        while (in.read(reinterpret_cast<char*>(marker), 4)) {
            if (marker[0] != 0xFF) return false;
            const unsigned char type = marker[1];
            if (type == 0xD8 || (type >= 0xD0 && type <= 0xD7) || type == 0x01) { in.seekg(-2, std::ios::cur); continue; }
            const uint16_t length = BigEndian16(marker + 2);
            if (length < 2) return false;
            const bool sof = (type >= 0xC0 && type <= 0xCF) && type != 0xC4 && type != 0xC8 && type != 0xCC;
            if (sof) {
                unsigned char frame[5];
                if (!in.read(reinterpret_cast<char*>(frame), 5)) return false;
                height = BigEndian16(frame + 1);
                width = BigEndian16(frame + 3);
                return width > 0 && height > 0;
            }
            if (type == 0xD9 || type == 0xDA) return false;
            in.seekg(length - 2, std::ios::cur);
        }
        return false;
    }
    return false;
}

std::string CleanMediaName(const std::string& fileName, const std::function<bool(const std::string&)>& taken) {
    const std::string ext = LowerExtension(fileName);
    const std::string stem = fileName.substr(0, ext.empty() ? fileName.size() : fileName.size() - ext.size() - 1);
    std::string base = MakeSlug(stem);
    if (base == "page") base = "file";   // MakeSlug's fallback for an empty name
    std::string candidate = base + (ext.empty() ? "" : "." + ext);
    for (int n = 2; taken && taken(candidate); ++n) {
        candidate = base + "-" + std::to_string(n) + (ext.empty() ? "" : "." + ext);
    }
    return candidate;
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
        if (file.kind == "image") ImageDimensions(UltraCanvas::PathToUtf8(entry.path()), file.width, file.height);
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
    const std::string candidate = CleanMediaName(UltraCanvas::PathToUtf8(source.filename()), [&](const std::string& name) {
        return fs::exists(media / UltraCanvas::PathFromUtf8(name), ec);
    });
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

bool PruneMedia(const std::string& siteDirectory, const Site& site, std::vector<std::string>& removed,
                std::vector<std::string>& errors, const BlockRegistry& registry) {
    bool ok = true;
    for (const MediaFile& file : ListMedia(siteDirectory, &site, registry)) {
        if (file.usedBy > 0) continue;
        std::string error;
        if (RemoveMedia(siteDirectory, file.path, error, &site, registry)) removed.push_back(file.path);
        else { errors.push_back(error); ok = false; }
    }
    return ok;
}

} // namespace UltraPress
