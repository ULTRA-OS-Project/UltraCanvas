// Tests/UltraStore/test_assets.cpp
// AssetCache: URLs resolved against the catalog's, files fetched once and
// kept.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "helpers.h"

#include "AssetCache.h"

#include "UltraCanvasPathUtf8.h"

#include <filesystem>
#include <fstream>
#include <map>

using namespace UltraStore;
using namespace ultrastore_test;

TEST(relative_urls_resolve_against_the_catalog) {
    REQUIRE_EQ(ResolveCatalogUrl("https://s.example/v1", "icons/a.png"), std::string("https://s.example/v1/icons/a.png"));
    REQUIRE_EQ(ResolveCatalogUrl("https://s.example/v1/", "icons/a.png"), std::string("https://s.example/v1/icons/a.png"));
    REQUIRE_EQ(ResolveCatalogUrl("https://s.example/v1/catalog.json", "a.png"), std::string("https://s.example/v1/a.png"));
    REQUIRE_EQ(ResolveCatalogUrl("https://s.example/v1", "/root.png"), std::string("https://s.example/root.png"));
    REQUIRE_EQ(ResolveCatalogUrl("https://s.example/v1", "https://cdn.example/x.png"), std::string("https://cdn.example/x.png"));
    REQUIRE_EQ(ResolveCatalogUrl("https://s.example/v1", ""), std::string());
    REQUIRE_EQ(ResolveCatalogUrl("", "a.png"), std::string());
}

TEST(the_cache_fetches_once_and_keeps_the_extension) {
    const std::string dir = TempRoot("assets");
    std::map<std::string, std::string> server = {{"https://s.example/icons/a.png", "PNGBYTES"}};
    int fetches = 0;
    AssetCache cache(dir, [&](const std::string& url, std::string& body, std::string& error) {
        ++fetches;
        auto it = server.find(url);
        if (it == server.end()) { error = "404"; return false; }
        body = it->second;
        return true;
    });
    const std::string url = "https://s.example/icons/a.png";
    REQUIRE(!cache.Has(url));
    const auto first = cache.Fetch(url);
    REQUIRE(first.Ok());
    REQUIRE(first.Value().size() > 4 && first.Value().substr(first.Value().size() - 4) == ".png");
    REQUIRE_EQ(first.Value(), cache.PathFor(url));
    REQUIRE(cache.Has(url));
    {
        std::ifstream in(UltraCanvas::PathFromUtf8(first.Value()), std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        REQUIRE_EQ(bytes, std::string("PNGBYTES"));
    }
    const auto second = cache.Fetch(url);
    REQUIRE(second.Ok());
    REQUIRE_EQ(fetches, 1);
    REQUIRE(!std::filesystem::exists(UltraCanvas::PathFromUtf8(first.Value() + ".tmp")));
    // A miss is NetworkError and leaves nothing behind.
    const auto missing = cache.Fetch("https://s.example/icons/none.png");
    REQUIRE(!missing.Ok());
    REQUIRE(missing.error.code == ErrorCode::NetworkError);
    REQUIRE(!cache.Has("https://s.example/icons/none.png"));
    // Two URLs with the same name are two files.
    server["https://s.example/other/a.png"] = "OTHER";
    REQUIRE(cache.PathFor("https://s.example/other/a.png") != cache.PathFor(url));
    // A URL without an extension gets none; a query string is not one.
    REQUIRE(cache.PathFor("https://s.example/icon").find('.') == std::string::npos ||
            cache.PathFor("https://s.example/icon").rfind('.') < dir.size());
    REQUIRE(cache.PathFor("https://s.example/a.png?v=2").substr(cache.PathFor("https://s.example/a.png?v=2").size() - 4) == ".png");
    std::error_code ec;
    std::filesystem::remove_all(UltraCanvas::PathFromUtf8(dir), ec);
}
