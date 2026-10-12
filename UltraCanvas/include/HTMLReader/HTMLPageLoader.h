// include/HTMLReader/HTMLPageLoader.h
// Turns a URL into a page the element builder can show: the document fetched
// and decoded to UTF-8 (BOM, Content-Type charset, <meta charset>, else UTF-8
// when it is valid and Windows-1252 when it is not), parsed, its <base href>
// applied, and its linked style sheets fetched before the page is ready - so
// the first build already has them. Pictures and other subresources are
// fetched on demand: GetResource() answers from a cache and queues what it
// does not have, and onResourcesArrived says when to build again.
//
// Framework-independent: fetching, URL resolution and charset conversion are
// services the host passes in (UltraWeb hands it UltraNet), so the loader
// runs offline in tests with a map of URL -> bytes.
//
// Threading: the loader is used from one thread. `fetch` may work anywhere,
// but must call its `done` on that thread (UltraWeb posts it to the UI
// thread).
//
// Rules for subresources: http(s) pages load http(s) and data: resources, an
// https page no http ones (mixed content), and only a file: page loads file:
// resources. Anything else is refused and logged in the page's warnings.
// Version: 1.2.0 - LoadPost and PageLoaderServices::post: the page a form's POST
//                  answers with
// Version: 1.1.0 - @import: the sheets a linked sheet or a <style> imports
//                  are fetched before the page is ready too (up to 4 deep,
//                  within maxStyleSheets), and RewriteCssUrls makes their
//                  addresses absolute
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#pragma once

#include "HTMLReader/HTMLDocument.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace UltraCanvas {
namespace HTML {

// What a fetch brought back. `ok` is the transport (a 404 with a body is ok);
// `finalUrl` is the address after redirects, empty for the one asked.
struct FetchResult {
    bool ok = false;
    int status = 0;
    std::string finalUrl;
    std::string contentType;
    std::vector<uint8_t> body;
    std::string error;
};

struct PageLoaderServices {
    // Fetch `url` (http, https or file); call `done` exactly once, on the
    // loader's thread.
    std::function<void(const std::string& url, std::function<void(FetchResult)> done)> fetch;
    // POST `body` (with `contentType`) to `url`, for a form; call `done`
    // exactly once, on the loader's thread. Without it a page's forms can
    // only send by GET.
    std::function<void(const std::string& url, const std::string& contentType, const std::string& body,
                       std::function<void(FetchResult)> done)> post;
    // `reference` read against `base` (RFC 3986); false when it cannot be.
    std::function<bool(const std::string& base, const std::string& reference,
                       std::string& absolute)> resolve;
    // `bytes` in `charset` as UTF-8 (UltraNet_ConvertToUtf8). Optional:
    // without it only UTF-8 and Latin-1 / Windows-1252 are read.
    std::function<std::string(const std::string& bytes, const std::string& charset)> toUtf8;
};

struct PageLoaderLimits {
    size_t maxDocumentBytes = 16u * 1024u * 1024u;   // larger documents are refused
    size_t maxStyleSheets = 32;                       // <link rel=stylesheet> and @import sheets per page
    size_t maxResources = 512;                        // subresources fetched per page
    size_t cacheBytes = 64u * 1024u * 1024u;          // resource cache, oldest out first
};

// What a navigation found.
enum class PageKind {
    Html,    // an HTML (or XHTML) document
    Text,    // text/plain, shown as a <pre> page
    Image,   // a picture, shown on a page of its own
    Other,   // anything else (application/wasm, a zip, ...): the host decides
    Error    // the fetch failed; `error` says why
};

struct LoadedPage {
    PageKind kind = PageKind::Error;
    std::string url;          // the address shown: final URL after redirects
    std::string baseUrl;      // what relative links resolve against (<base href>)
    std::string contentType;
    int status = 0;
    std::string error;
    // The page, parsed (Html, Text and Image; null for Other and Error).
    // Kept so a rebuild - a new width, pictures that arrived - needs no
    // second parse.
    std::shared_ptr<Document> document;
    std::string charset;      // what the bytes were read as
    std::vector<std::string> warnings;
};

class PageLoader {
public:
    explicit PageLoader(PageLoaderServices services, PageLoaderLimits limits = {});
    ~PageLoader();
    PageLoader(const PageLoader&) = delete;
    PageLoader& operator=(const PageLoader&) = delete;

    // Starts loading `url`; the navigation before it is cancelled, so its
    // answer never arrives. `onReady` is called once, with the page or the
    // error. Returns the navigation's number.
    uint64_t Load(const std::string& url, std::function<void(LoadedPage)> onReady);
    // The page a form's POST answers with: `body` sent as `contentType` to
    // `url` (PageLoaderServices::post), the answer read as Load reads one.
    uint64_t LoadPost(const std::string& url, const std::string& contentType, const std::string& body,
                      std::function<void(LoadedPage)> onReady);
    // Drops the navigation in progress (and the resources it queued).
    void Cancel();
    bool IsLoading() const;

    // For BuildOptions::resourceLoader: `href` as the document writes it,
    // resolved against the current page's base. Answers from the cache;
    // what is not there (and is allowed) is fetched, and onResourcesArrived
    // fires once it is.
    std::vector<uint8_t> GetResource(const std::string& href);
    // A queued resource arrived (or failed): build the page again.
    std::function<void()> onResourcesArrived;
    size_t PendingResources() const;

    // `href` resolved against the current page's base; "" when it cannot be.
    std::string Resolve(const std::string& href) const;
    const std::string& BaseUrl() const;

    // The charset to read a document in: BOM, then the Content-Type
    // charset, then <meta charset> / http-equiv in the first 1024 bytes,
    // then "utf-8" when the bytes are valid UTF-8, else "windows-1252".
    static std::string DetectCharset(const std::vector<uint8_t>& bytes,
                                     const std::string& contentType);
    // Every url(...) in a style sheet, and every @import "..." address,
    // made absolute against the sheet's own URL, so it means the same once
    // the sheet is part of the page.
    static std::string RewriteCssUrls(const std::string& css, const std::string& sheetUrl,
                                      const std::function<bool(const std::string&, const std::string&,
                                                               std::string&)>& resolve);
    // A data: URL's bytes (base64 or percent-encoded); false when malformed.
    static bool DecodeDataUrl(const std::string& url, std::vector<uint8_t>& out);

private:
    struct Navigation;
    struct CacheEntry {
        enum class State { Pending, Done, Failed } state = State::Pending;
        std::vector<uint8_t> bytes;
    };

    struct PostRequest {
        std::string contentType;
        std::string body;
    };
    uint64_t Start(const std::string& url, std::function<void(LoadedPage)> onReady, const PostRequest* post);
    void Finish(const std::shared_ptr<Navigation>& nav, FetchResult result);
    void FetchStyleSheets(const std::shared_ptr<Navigation>& nav);
    // Fetch one sheet for the navigation (`depth` imports below the page),
    // then the sheets it imports; the page is ready when none is pending.
    void FetchSheet(const std::shared_ptr<Navigation>& nav, const std::string& url, int depth);
    // Queue the sheets `css` imports, read against `sheetUrl`.
    void FetchImports(const std::shared_ptr<Navigation>& nav, const std::string& css,
                      const std::string& sheetUrl, int depth);
    void SheetDone(const std::shared_ptr<Navigation>& nav);
    std::string ToUtf8(const std::vector<uint8_t>& bytes, const std::string& charset) const;
    bool Allowed(const std::string& absolute, std::string& why) const;
    void Store(const std::string& url, CacheEntry entry);

    PageLoaderServices services_;
    PageLoaderLimits limits_;
    std::shared_ptr<Navigation> current_;
    // Bumped by every Load / Cancel; a callback carrying an older one is
    // from a navigation the user has left.
    std::shared_ptr<uint64_t> generation_;
    std::string pageUrl_;
    std::string baseUrl_;
    std::unordered_map<std::string, CacheEntry> cache_;
    std::deque<std::string> cacheOrder_;
    size_t cacheBytes_ = 0;
    size_t pending_ = 0;
    size_t requested_ = 0;   // subresources asked for on this page
};

} // namespace HTML
} // namespace UltraCanvas
