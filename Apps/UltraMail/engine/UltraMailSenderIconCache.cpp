// Apps/UltraMail/engine/UltraMailSenderIconCache.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailSenderIconCache.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace UltraMail {

namespace {

// The extensions a cached icon may have, newest lookup first. Kept in one
// place so IconForBrand and Store cannot disagree about where a file lands.
const char* const kExtensions[] = { "png", "ico", "svg", "jpg", "gif", "webp" };

bool StartsWith(const std::vector<uint8_t>& b, std::initializer_list<uint8_t> magic,
                std::size_t offset = 0) {
    if (b.size() < offset + magic.size()) return false;
    std::size_t i = offset;
    for (uint8_t m : magic) if (b[i++] != m) return false;
    return true;
}

int64_t Now() { return static_cast<int64_t>(std::time(nullptr)); }

} // namespace

std::string SniffImageExtension(const std::vector<uint8_t>& b) {
    if (b.size() < 8) return "";
    if (StartsWith(b, { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A })) return "png";
    if (StartsWith(b, { 0xFF, 0xD8, 0xFF }))                            return "jpg";
    if (StartsWith(b, { 'G', 'I', 'F', '8' }))                          return "gif";
    if (StartsWith(b, { 0x00, 0x00, 0x01, 0x00 }))                      return "ico";
    if (StartsWith(b, { 'R', 'I', 'F', 'F' }) && StartsWith(b, { 'W', 'E', 'B', 'P' }, 8))
        return "webp";
    // SVG: an XML or <svg opening within the first bytes.
    const std::string head(b.begin(), b.begin() + std::min<std::size_t>(b.size(), 200));
    if (head.find("<svg") != std::string::npos ||
        (head.find("<?xml") != std::string::npos && head.find("svg") != std::string::npos))
        return "svg";
    return "";
}

void SenderIconCache::SetRoot(std::string directory) {
    std::lock_guard<std::mutex> lock(mutex_);
    root_ = std::move(directory);
    resolved_.clear();
}

std::string SenderIconCache::Root() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return root_;
}

void SenderIconCache::SetFetcher(Fetcher fetcher) {
    std::lock_guard<std::mutex> lock(mutex_);
    fetcher_ = std::move(fetcher);
}

void SenderIconCache::SetNetworkEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    network_ = enabled;
}

bool SenderIconCache::NetworkEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return network_;
}

void SenderIconCache::SetRetryInterval(int64_t seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    retrySeconds_ = seconds;
}

std::string SenderIconCache::PathFor(const std::string& brandId) const {
    if (root_.empty() || brandId.empty()) return "";
    if (const auto memo = resolved_.find(brandId); memo != resolved_.end())
        return memo->second;
    std::error_code ec;
    std::string found;
    for (const char* ext : kExtensions) {
        const fs::path p = fs::path(root_) / (brandId + "." + ext);
        if (fs::exists(p, ec)) { found = p.string(); break; }
    }
    resolved_[brandId] = found;   // a miss is remembered too; Store() clears it
    return found;
}

std::string SenderIconCache::IconForBrand(const std::string& brandId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return PathFor(brandId);
}

std::string SenderIconCache::IconForAddress(const std::string& address) const {
    const SenderBrand* brand = BrandForAddress(address);
    return brand ? IconForBrand(brand->id) : std::string();
}

bool SenderIconCache::RetryAllowed(const std::string& brandId) const {
    if (root_.empty()) return false;
    const fs::path marker = fs::path(root_) / (brandId + ".missing");
    std::error_code ec;
    if (!fs::exists(marker, ec)) return true;
    std::ifstream in(marker);
    int64_t when = 0;
    in >> when;
    return Now() - when >= retrySeconds_;
}

void SenderIconCache::NoteFailure(const std::string& brandId) {
    if (root_.empty()) return;
    std::error_code ec;
    fs::create_directories(root_, ec);
    std::ofstream out(fs::path(root_) / (brandId + ".missing"), std::ios::trunc);
    if (out) out << Now() << "\n";
}

std::string SenderIconCache::Store(const std::string& brandId,
                                   const std::vector<uint8_t>& bytes) {
    const std::string ext = SniffImageExtension(bytes);
    std::lock_guard<std::mutex> lock(mutex_);
    if (root_.empty() || brandId.empty() || ext.empty()) return "";

    std::error_code ec;
    fs::create_directories(root_, ec);
    const fs::path path = fs::path(root_) / (brandId + "." + ext);
    // Write beside the target and rename, so a half-written icon is never seen
    // by the UI thread reading the same folder.
    const fs::path temp = fs::path(root_) / (brandId + ".part");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return "";
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        if (!out) return "";
    }
    fs::rename(temp, path, ec);
    if (ec) { fs::remove(temp, ec); return ""; }
    fs::remove(fs::path(root_) / (brandId + ".missing"), ec);
    resolved_[brandId] = path.string();
    return path.string();
}

std::string SenderIconCache::EnsureIconForBrand(const SenderBrand& brand) {
    Fetcher fetcher;
    std::string url;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (const std::string cached = PathFor(brand.id); !cached.empty()) return cached;
        if (!network_ || !fetcher_ || brand.iconUrl.empty()) return "";
        if (!RetryAllowed(brand.id)) return "";
        fetcher = fetcher_;
        url     = brand.iconUrl;
    }

    // The fetch runs outside the lock: it is a network round trip, and the UI
    // thread must be able to read cached icons while it is in flight.
    std::vector<uint8_t> bytes;
    if (!fetcher(url, bytes) || bytes.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        NoteFailure(brand.id);
        return "";
    }
    const std::string stored = Store(brand.id, bytes);
    if (stored.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        NoteFailure(brand.id);   // a body that is not an image is a failure too
    }
    return stored;
}

std::string SenderIconCache::EnsureIconForAddress(const std::string& address) {
    const SenderBrand* brand = BrandForAddress(address);
    return brand ? EnsureIconForBrand(*brand) : std::string();
}

int SenderIconCache::WarmAll() {
    int stored = 0;
    for (const auto& brand : KnownBrands())
        if (!EnsureIconForBrand(brand).empty()) ++stored;
    return stored;
}

int SenderIconCache::CachedCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (root_.empty()) return 0;
    int count = 0;
    for (const auto& brand : KnownBrands())
        if (!PathFor(brand.id).empty()) ++count;
    return count;
}

} // namespace UltraMail
