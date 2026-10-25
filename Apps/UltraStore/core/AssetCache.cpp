// Apps/UltraStore/core/AssetCache.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "AssetCache.h"

#include "Verifier.h"

#include "UltraCanvasPathUtf8.h"
#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetHttp.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace UltraStore {

namespace {

bool HasScheme(const std::string& url) {
    const size_t colon = url.find("://");
    if (colon == std::string::npos || colon == 0) return false;
    for (size_t i = 0; i < colon; ++i) {
        if (!std::isalnum(static_cast<unsigned char>(url[i])) && url[i] != '+' && url[i] != '-' && url[i] != '.') return false;
    }
    return true;
}

// The extension of the URL's last path segment, lower-case, "" when there is
// none that looks like one (letters and digits, at most five).
std::string ExtensionOf(const std::string& url) {
    std::string path = url;
    const size_t query = path.find_first_of("?#");
    if (query != std::string::npos) path.resize(query);
    const size_t slash = path.find_last_of('/');
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0 || name.size() - dot - 1 > 5) return {};
    std::string ext = name.substr(dot);
    for (char& c : ext) {
        if (c != '.' && !std::isalnum(static_cast<unsigned char>(c))) return {};
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext;
}

} // namespace

std::string ResolveCatalogUrl(const std::string& catalogBaseUrl, const std::string& relative) {
    if (relative.empty()) return {};
    if (HasScheme(relative)) return relative;
    if (catalogBaseUrl.empty()) return {};
    std::string base = catalogBaseUrl;
    // The base may name the catalog file itself.
    if (base.size() >= 13 && base.compare(base.size() - 13, 13, "/catalog.json") == 0) base.resize(base.size() - 13);
    while (!base.empty() && base.back() == '/') base.pop_back();
    if (relative[0] == '/') {
        const size_t scheme = base.find("://");
        const size_t hostEnd = scheme == std::string::npos ? std::string::npos : base.find('/', scheme + 3);
        return (hostEnd == std::string::npos ? base : base.substr(0, hostEnd)) + relative;
    }
    return base + "/" + relative;
}

AssetFetcher UltraNetAssetFetcher() {
    return [](const std::string& url, std::string& bodyOut, std::string& error) {
        if (!UltraNet_IsInitialized()) UltraNet_Initialize();
        UltraNetHttpOptions options;
        options.httpsOnlyRedirects = true;
        options.maxReceiveSize = int64_t(16) << 20;
        UltraNetResponse response;
        const UltraNetResult r = UltraNet_HttpGet(url, response, options);
        if (!r && r.code != UltraNetResultCode::HttpError) {
            error = r.message.empty() ? "no answer" : r.message;
            return false;
        }
        if (!response.IsComplete()) { error = response.transferError; return false; }
        if (!response.IsSuccess()) { error = "HTTP " + std::to_string(response.statusCode); return false; }
        bodyOut = response.GetBodyAsString();
        return true;
    };
}

AssetCache::AssetCache(std::string dirUtf8, AssetFetcher fetcher)
    : dir_(std::move(dirUtf8)), fetcher_(std::move(fetcher)) {}

std::string AssetCache::PathFor(const std::string& url) const {
    return UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(dir_) / UltraCanvas::PathFromUtf8(Sha256Hex(url) + ExtensionOf(url)));
}

bool AssetCache::Has(const std::string& url) const {
    std::error_code ec;
    return std::filesystem::is_regular_file(UltraCanvas::PathFromUtf8(PathFor(url)), ec);
}

Result<std::string> AssetCache::Fetch(const std::string& url) {
    using R = Result<std::string>;
    if (url.empty()) return R::Failure(ErrorCode::InvalidArgument, "no URL");
    const std::string path = PathFor(url);
    if (Has(url)) return R::Succeed(path);
    std::string body, error;
    if (!fetcher_ || !fetcher_(url, body, error)) return R::Failure(ErrorCode::NetworkError, "could not fetch " + url, error);
    std::error_code ec;
    std::filesystem::create_directories(UltraCanvas::PathFromUtf8(dir_), ec);
    const std::filesystem::path target = UltraCanvas::PathFromUtf8(path);
    const std::filesystem::path temp = UltraCanvas::PathFromUtf8(path + ".tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return R::Failure(ErrorCode::IoError, "cannot write " + path);
        out.write(body.data(), static_cast<std::streamsize>(body.size()));
        if (!out) return R::Failure(ErrorCode::IoError, "cannot write " + path);
    }
    std::filesystem::remove(target, ec);
    ec.clear();
    std::filesystem::rename(temp, target, ec);
    if (ec) return R::Failure(ErrorCode::IoError, "cannot place " + path, ec.message());
    return R::Succeed(path);
}

} // namespace UltraStore
