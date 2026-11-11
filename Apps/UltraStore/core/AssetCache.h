// Apps/UltraStore/core/AssetCache.h
// The catalog's pictures - an app's icon and screenshots - fetched once and
// kept on disk under the catalog cache, so the window shows them again
// offline and never asks the server twice for the same file. A URL in the
// catalog is relative to the catalog's own URL (`icons/ultramail.png` next
// to `catalog.json`); ResolveCatalogUrl makes it absolute. The fetch is a
// function the cache is given, so the tests hand it a map and nothing here
// touches the network in a test; the default fetches with UltraNet.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraStoreTypes.h"

#include <functional>
#include <string>

namespace UltraStore {

// "https://store.example/v1" + "icons/a.png" -> "https://store.example/v1/icons/a.png";
// an absolute URL (with a scheme) passes through; "/x" is from the host's
// root; an empty relative gives an empty string.
std::string ResolveCatalogUrl(const std::string& catalogBaseUrl, const std::string& relative);

// GET url into bodyOut; false with an error message when nothing complete came.
using AssetFetcher = std::function<bool(const std::string& url, std::string& bodyOut, std::string& error)>;

// The default: UltraNet, redirects to https:// only, 16 MB cap.
AssetFetcher UltraNetAssetFetcher();

class AssetCache {
public:
    explicit AssetCache(std::string dirUtf8, AssetFetcher fetcher = UltraNetAssetFetcher());

    // Where the URL's file lives (or would live): <dir>/<sha256 of the url><extension of the url>.
    std::string PathFor(const std::string& url) const;
    bool Has(const std::string& url) const;

    // The file's path: from the cache when it is there, fetched and written
    // (beside and renamed into place) when not. NetworkError or IoError.
    Result<std::string> Fetch(const std::string& url);

    const std::string& Dir() const { return dir_; }

private:
    std::string dir_;
    AssetFetcher fetcher_;
};

} // namespace UltraStore
