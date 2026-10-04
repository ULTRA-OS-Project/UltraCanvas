// Apps/UltraMail/engine/UltraMailAttachmentCache.cpp
// Version: 0.2.0 - Prune(); paths through PathFromUtf8
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailAttachmentCache.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

namespace fs = std::filesystem;

namespace UltraMail {

namespace {

// A minimal media-type -> extension fallback for attachments that arrive
// without a usable filename extension.
std::string ExtForMediaType(const std::string& mt) {
    std::string m = mt;
    std::transform(m.begin(), m.end(), m.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (m == "image/png")       return ".png";
    if (m == "image/jpeg")      return ".jpg";
    if (m == "image/gif")       return ".gif";
    if (m == "image/webp")      return ".webp";
    if (m == "image/bmp")       return ".bmp";
    if (m == "image/svg+xml")   return ".svg";
    if (m == "application/pdf") return ".pdf";
    if (m == "text/plain")      return ".txt";
    if (m == "text/html")       return ".html";
    if (m == "text/csv")        return ".csv";
    if (m == "application/zip") return ".zip";
    return ".bin";
}

bool WriteBytes(const fs::path& path, const std::vector<uint8_t>& data) {
    std::ofstream os(path, std::ios::binary | std::ios::trunc);
    if (!os) return false;
    if (!data.empty())
        os.write(reinterpret_cast<const char*>(data.data()),
                 static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(os);
}

bool SameContent(const fs::path& path, const std::vector<uint8_t>& data) {
    std::error_code ec;
    if (!fs::exists(path, ec)) return false;
    if (fs::file_size(path, ec) != data.size()) return false;
    std::ifstream is(path, std::ios::binary);
    if (!is) return false;
    std::vector<char> buf((std::istreambuf_iterator<char>(is)),
                          std::istreambuf_iterator<char>());
    return buf.size() == data.size() &&
           std::equal(buf.begin(), buf.end(),
                      reinterpret_cast<const char*>(data.data()));
}

} // namespace

std::string AttachmentCache::SanitizeFilename(const std::string& name,
                                              const std::string& mediaType) {
    // Strip any directory component (path-traversal safety).
    std::string base = name;
    std::size_t slash = base.find_last_of("/\\");
    if (slash != std::string::npos) base = base.substr(slash + 1);

    // Replace control / unsafe characters with '_'.
    std::string clean;
    for (unsigned char c : base) {
        if (c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            clean.push_back('_');
        else
            clean.push_back(static_cast<char>(c));
    }
    // Trim dots/spaces that would make an odd or hidden name.
    while (!clean.empty() && (clean.front() == '.' || clean.front() == ' ')) clean.erase(clean.begin());
    while (!clean.empty() && (clean.back() == ' ')) clean.pop_back();
    if (clean.empty()) clean = "attachment";

    // Ensure an extension so the viewer can pick a renderer.
    if (clean.find('.') == std::string::npos)
        clean += ExtForMediaType(mediaType);
    return clean;
}

std::string AttachmentCache::Write(const Attachment& attachment) const {
    std::error_code ec;
    const fs::path dir = PathFromUtf8(cacheDir_);
    fs::create_directories(dir, ec);

    const std::string safe = SanitizeFilename(attachment.filename, attachment.mediaType);
    const std::string stem = PathToUtf8(PathFromUtf8(safe).stem());
    const std::string ext  = PathToUtf8(PathFromUtf8(safe).extension());

    // Reuse an identical existing file; otherwise pick a free suffixed name.
    for (int i = 0; i < 10000; ++i) {
        const fs::path candidate =
            dir / PathFromUtf8(i == 0 ? safe : (stem + " (" + std::to_string(i) + ")" + ext));
        if (!fs::exists(candidate, ec))
            return WriteBytes(candidate, attachment.data) ? PathToUtf8(candidate) : std::string();
        if (SameContent(candidate, attachment.data)) {
            // Already cached: opened again just now, so not a pruning candidate.
            fs::last_write_time(candidate, fs::file_time_type::clock::now(), ec);
            return PathToUtf8(candidate);
        }
    }
    return std::string();
}

bool AttachmentCache::SaveAs(const Attachment& attachment, const std::string& destPath) const {
    std::error_code ec;
    fs::path p(UltraCanvas::PathFromUtf8(destPath));
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    return WriteBytes(p, attachment.data);
}

AttachmentPruneStats AttachmentCache::Prune(int64_t maxAgeSeconds, uint64_t maxBytes) const {
    AttachmentPruneStats stats;
    std::error_code ec;
    const fs::path dir = PathFromUtf8(cacheDir_);
    if (!fs::is_directory(dir, ec)) return stats;

    struct Entry { fs::path path; fs::file_time_type written; uint64_t size; };
    std::vector<Entry> files;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;   // subdirectories are not ours
        Entry e{ entry.path(), entry.last_write_time(ec), 0 };
        if (ec) { ec.clear(); continue; }
        e.size = static_cast<uint64_t>(entry.file_size(ec));
        if (ec) { ec.clear(); e.size = 0; }
        files.push_back(std::move(e));
    }
    // Oldest first: the age rule takes a prefix, the size rule continues it.
    std::sort(files.begin(), files.end(),
              [](const Entry& a, const Entry& b) { return a.written < b.written; });

    uint64_t total = 0;
    for (const Entry& e : files) total += e.size;

    const auto now = fs::file_time_type::clock::now();
    auto drop = [&](const Entry& e) {
        std::error_code rc;
        if (!fs::remove(e.path, rc)) return false;   // e.g. still open on Windows
        ++stats.removed;
        stats.bytesRemoved += e.size;
        total -= e.size;
        return true;
    };
    for (const Entry& e : files) {
        const bool tooOld = maxAgeSeconds < 0 ||
            (maxAgeSeconds > 0 && now - e.written > std::chrono::seconds(maxAgeSeconds));
        const bool overCap = maxBytes > 0 && total > maxBytes;
        if (tooOld || overCap) drop(e);
    }
    return stats;
}

} // namespace UltraMail
