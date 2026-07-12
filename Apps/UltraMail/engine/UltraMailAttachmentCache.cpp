// Apps/UltraMail/engine/UltraMailAttachmentCache.cpp
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailAttachmentCache.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>

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
    fs::create_directories(cacheDir_, ec);

    const std::string safe = SanitizeFilename(attachment.filename, attachment.mediaType);
    const fs::path dir(cacheDir_);
    const std::string stem = fs::path(safe).stem().string();
    const std::string ext  = fs::path(safe).extension().string();

    // Reuse an identical existing file; otherwise pick a free suffixed name.
    for (int i = 0; i < 10000; ++i) {
        fs::path candidate = dir / (i == 0 ? safe : (stem + " (" + std::to_string(i) + ")" + ext));
        if (!fs::exists(candidate, ec))
            return WriteBytes(candidate, attachment.data) ? candidate.string() : std::string();
        if (SameContent(candidate, attachment.data))
            return candidate.string();   // already cached
    }
    return std::string();
}

bool AttachmentCache::SaveAs(const Attachment& attachment, const std::string& destPath) const {
    std::error_code ec;
    fs::path p(destPath);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    return WriteBytes(p, attachment.data);
}

} // namespace UltraMail
