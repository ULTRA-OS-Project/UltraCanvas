// Apps/UltraMail/engine/UltraMailSenderIconCache.cpp
// Version: 0.2.0 - the loader threads (Request), website icons, keys
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailSenderIconCache.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>
#include <utility>

using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

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

constexpr const char* kSitePrefix = "site:";
// Website icons have a folder of their own, so a domain can never be taken
// for a brand id or the other way round.
constexpr const char* kSiteFolder = "sites";
// Loader threads at most; each ends after this long with nothing to do.
constexpr int kMaxLoaders = 3;
constexpr auto kLoaderIdle = std::chrono::seconds(20);
// A website's icon: its home page, then at most this many icon URLs.
constexpr int kSiteIconTries = 4;

bool IsSiteKey(const std::string& key) { return key.rfind(kSitePrefix, 0) == 0; }

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool StartsWithNoCase(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && Lower(s.substr(0, prefix.size())) == prefix;
}

// A host name that is safe as a file name and can be a website's.
bool ValidSiteDomain(const std::string& d) {
    if (d.empty() || d.size() > 253 || d.find('.') == std::string::npos) return false;
    if (d.front() == '.' || d.back() == '.' || d.front() == '-' ||
        d.find("..") != std::string::npos)
        return false;
    for (char c : d)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-'))
            return false;
    // An IP address has no website icon worth asking for.
    const std::string last = d.substr(d.rfind('.') + 1);
    return !std::all_of(last.begin(), last.end(),
                        [](char c) { return c >= '0' && c <= '9'; });
}

// A registry brand id: what the brand table uses, and safe as a file name.
bool ValidBrandId(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return false;
    return true;
}

bool ValidKey(const std::string& key) {
    return IsSiteKey(key) ? ValidSiteDomain(key.substr(5)) : ValidBrandId(key);
}

// ---- URLs ----------------------------------------------------------------
// "https://www.example.com/a/b.html?x#y" -> scheme "https", origin
// "https://www.example.com", path "/a/b.html". False when it is no http(s) URL.
bool SplitUrl(const std::string& url, std::string& scheme, std::string& origin,
              std::string& path) {
    const std::size_t colon = url.find("://");
    if (colon == std::string::npos) return false;
    scheme = Lower(url.substr(0, colon));
    if (scheme != "http" && scheme != "https") return false;
    const std::size_t hostStart = colon + 3;
    std::size_t pathStart = url.find_first_of("/?#", hostStart);
    if (pathStart == std::string::npos) pathStart = url.size();
    if (pathStart == hostStart) return false;   // no host
    origin = scheme + "://" + url.substr(hostStart, pathStart - hostStart);
    path = url.substr(pathStart);
    const std::size_t cut = path.find_first_of("?#");
    if (cut != std::string::npos) path.erase(cut);
    if (path.empty() || path.front() != '/') path = "/" + path;
    return true;
}

// "/a/./b/../c" -> "/a/c".
std::string RemoveDotSegments(const std::string& path) {
    std::vector<std::string> out;
    std::size_t start = 1;
    while (start <= path.size()) {
        std::size_t slash = path.find('/', start);
        if (slash == std::string::npos) slash = path.size();
        const std::string seg = path.substr(start, slash - start);
        if (seg == "..") { if (!out.empty()) out.pop_back(); }
        else if (seg != ".") out.push_back(seg);
        start = slash + 1;
    }
    std::string result;
    for (const auto& seg : out) result += "/" + seg;
    // "/a/b/" keeps its trailing slash ("/a/b/." as well).
    if (!path.empty() && (path.back() == '/' ||
                          (path.size() >= 2 && path.compare(path.size() - 2, 2, "/.") == 0)))
        result += "/";
    return result.empty() ? "/" : result;
}

// `href` resolved against `base`; "" unless it is an http(s) URL.
std::string ResolveUrl(const std::string& base, const std::string& href) {
    if (href.empty()) return "";
    if (StartsWithNoCase(href, "http://") || StartsWithNoCase(href, "https://"))
        return href;
    std::string scheme, origin, path;
    if (!SplitUrl(base, scheme, origin, path)) return "";
    if (href.compare(0, 2, "//") == 0) return scheme + ":" + href;
    // Any other scheme (data:, javascript:, ftp:) is not fetched.
    const std::size_t colon = href.find(':');
    if (colon != std::string::npos && colon < href.find_first_of("/?#")) return "";
    std::string rest = href, suffix;
    const std::size_t cut = rest.find_first_of("?#");
    if (cut != std::string::npos) { suffix = rest.substr(cut); rest.erase(cut); }
    std::string joined;
    if (!rest.empty() && rest.front() == '/') joined = rest;
    else joined = path.substr(0, path.rfind('/') + 1) + rest;
    return origin + RemoveDotSegments(joined) + suffix;
}

// The attributes of one tag, from just after its name to its '>'; names
// lowercased. `end` is left after the '>'.
std::map<std::string, std::string> TagAttributes(const std::string& html, std::size_t pos,
                                                 std::size_t& end) {
    std::map<std::string, std::string> attrs;
    const std::size_t n = html.size();
    while (pos < n) {
        while (pos < n && (std::isspace(static_cast<unsigned char>(html[pos])) || html[pos] == '/'))
            ++pos;
        if (pos >= n || html[pos] == '>') { ++pos; break; }
        const std::size_t nameStart = pos;
        while (pos < n && !std::isspace(static_cast<unsigned char>(html[pos])) &&
               html[pos] != '=' && html[pos] != '>' && html[pos] != '/')
            ++pos;
        std::string name = Lower(html.substr(nameStart, pos - nameStart));
        while (pos < n && std::isspace(static_cast<unsigned char>(html[pos]))) ++pos;
        std::string value;
        if (pos < n && html[pos] == '=') {
            ++pos;
            while (pos < n && std::isspace(static_cast<unsigned char>(html[pos]))) ++pos;
            if (pos < n && (html[pos] == '"' || html[pos] == '\'')) {
                const char quote = html[pos++];
                const std::size_t close = html.find(quote, pos);
                const std::size_t stop = close == std::string::npos ? n : close;
                value = html.substr(pos, stop - pos);
                pos = stop == n ? n : stop + 1;
            } else {
                const std::size_t valueStart = pos;
                while (pos < n && !std::isspace(static_cast<unsigned char>(html[pos])) &&
                       html[pos] != '>')
                    ++pos;
                value = html.substr(valueStart, pos - valueStart);
            }
        }
        if (!name.empty() && !attrs.count(name)) attrs[name] = value;
    }
    end = pos;
    return attrs;
}

std::string DecodeAmpersands(std::string s) {
    for (std::size_t at = s.find("&amp;"); at != std::string::npos; at = s.find("&amp;", at + 1))
        s.replace(at, 5, "&");
    return s;
}

std::vector<std::string> Words(const std::string& s) {
    std::vector<std::string> words;
    std::string word;
    for (char c : s + " ") {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!word.empty()) words.push_back(word);
            word.clear();
        } else {
            word += c;
        }
    }
    return words;
}

// The largest edge a sizes="16x16 32x32" names; 0 when none (or "any").
int LargestSize(const std::string& sizes) {
    int best = 0;
    for (const std::string& token : Words(Lower(sizes))) {
        const std::size_t x = token.find('x');
        if (x == std::string::npos || x == 0) continue;
        const int edge = std::atoi(token.substr(0, x).c_str());
        best = std::max(best, edge);
    }
    return best;
}

} // namespace

std::string SiteIconKey(const std::string& domain) {
    const std::string reg = RegistrableDomain(Lower(domain));
    return ValidSiteDomain(reg) ? std::string(kSitePrefix) + reg : std::string();
}

std::vector<std::string> FindSiteIconUrls(const std::string& html, const std::string& pageUrl) {
    // Only the head names icons; a page that never closes it is read for
    // its first 512 KB.
    std::string lower = Lower(html.substr(0, 512 * 1024));
    std::size_t headEnd = std::min(lower.find("</head"), lower.find("<body"));
    if (headEnd == std::string::npos) headEnd = lower.size();

    struct Candidate { int tier; int order; std::string url; };
    std::vector<Candidate> found;
    for (std::size_t at = lower.find("<link"); at != std::string::npos && at < headEnd;
         at = lower.find("<link", at + 5)) {
        std::size_t end = 0;
        const auto attrs = TagAttributes(html, at + 5, end);
        const auto rel = attrs.find("rel");
        const auto href = attrs.find("href");
        if (rel == attrs.end() || href == attrs.end()) continue;
        bool icon = false, touch = false;
        for (const std::string& word : Words(Lower(rel->second))) {
            if (word == "icon") icon = true;
            if (word == "apple-touch-icon" || word == "apple-touch-icon-precomposed") touch = true;
        }
        if (!icon && !touch) continue;   // mask-icon is a one-colour outline
        std::string trimmed = DecodeAmpersands(href->second);
        trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
        trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);
        const std::string url = ResolveUrl(pageUrl, trimmed);
        if (url.empty()) continue;

        const auto type = attrs.find("type");
        std::string path, scheme, origin;
        SplitUrl(url, scheme, origin, path);
        const bool svg = (type != attrs.end() && Lower(type->second).find("svg") != std::string::npos) ||
                         (path.size() > 4 && Lower(path.substr(path.size() - 4)) == ".svg");
        const auto sizes = attrs.find("sizes");
        const int size = sizes == attrs.end() ? 0 : LargestSize(sizes->second);

        // Best first: a raster icon big enough for a badge on a high-density
        // screen, nearest 64 px; then bigger ones; the home-screen icon; an
        // icon of no stated size; SVG (not every platform draws it); tiny ones.
        Candidate c{ 0, 0, url };
        if (touch && !icon)          { c.tier = 2; c.order = 0; }
        else if (svg)                { c.tier = 4; c.order = 0; }
        else if (size >= 32 && size <= 256) { c.tier = 0; c.order = std::abs(size - 64); }
        else if (size > 256)         { c.tier = 1; c.order = size; }
        else if (size == 0)          { c.tier = 3; c.order = 0; }
        else                         { c.tier = 5; c.order = 32 - size; }
        found.push_back(c);
        at = std::max(at, end > 5 ? end - 5 : at);
    }
    std::stable_sort(found.begin(), found.end(), [](const Candidate& a, const Candidate& b) {
        return a.tier != b.tier ? a.tier < b.tier : a.order < b.order;
    });

    std::vector<std::string> urls;
    auto add = [&urls](const std::string& url) {
        if (!url.empty() && std::find(urls.begin(), urls.end(), url) == urls.end())
            urls.push_back(url);
    };
    for (const auto& c : found) add(c.url);
    std::string scheme, origin, path;
    if (SplitUrl(pageUrl, scheme, origin, path)) add(origin + "/favicon.ico");
    return urls;
}

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

// ---------------------------------------------------------------------------
// The loader
// ---------------------------------------------------------------------------
// Shared by the cache and its loader threads. The threads are detached and
// hold it, not the cache: a cache that goes away sets `owner` to null, and a
// thread whose download is still in flight then drops it and ends.
struct SenderIconCache::Loader {
    std::mutex              mutex;
    std::condition_variable wake;
    std::deque<std::string> queue;
    SenderIconCache*        owner    = nullptr;
    int                     threads  = 0;
    int                     idle     = 0;
    int                     inFlight = 0;
};

namespace {

// One key's download, outside every lock: a registry icon, or a website's
// (its home page's <link rel="icon">, then /favicon.ico).
bool CarryOut(const std::string& iconUrl, const std::string& siteDomain,
              const SenderIconCache::Fetcher& fetcher,
              const SenderIconCache::PageFetcher& pageFetcher,
              std::vector<uint8_t>& bytes) {
    if (!iconUrl.empty())
        return fetcher(iconUrl, bytes) && !SniffImageExtension(bytes).empty();

    std::vector<std::string> urls;
    if (pageFetcher) {
        for (const std::string& page : { "https://" + siteDomain + "/",
                                         "https://www." + siteDomain + "/" }) {
            std::string html, finalUrl;
            if (pageFetcher(page, html, finalUrl) && !html.empty()) {
                urls = FindSiteIconUrls(html, finalUrl.empty() ? page : finalUrl);
                break;
            }
        }
    }
    if (urls.empty())
        urls = { "https://" + siteDomain + "/favicon.ico",
                 "https://www." + siteDomain + "/favicon.ico" };
    int tries = 0;
    for (const std::string& url : urls) {
        if (tries++ == kSiteIconTries) break;
        bytes.clear();
        if (fetcher(url, bytes) && !SniffImageExtension(bytes).empty()) return true;
    }
    bytes.clear();
    return false;
}

} // namespace

void RunIconLoader(std::shared_ptr<SenderIconCache::Loader> loader) {
    std::unique_lock<std::mutex> lock(loader->mutex);
    for (;;) {
        ++loader->idle;
        const bool woken = loader->wake.wait_for(lock, kLoaderIdle, [&loader] {
            return !loader->owner || !loader->queue.empty();
        });
        --loader->idle;
        if (!loader->owner || !woken) { --loader->threads; return; }

        const std::string key = loader->queue.front();
        loader->queue.pop_front();
        // Lock order: the loader's, then the cache's (PlanFetch, Finish).
        const SenderIconCache::FetchPlan plan = loader->owner->PlanFetch(key);
        if (plan.Empty()) continue;
        ++loader->inFlight;
        lock.unlock();

        std::vector<uint8_t> bytes;
        const bool fetched = CarryOut(plan.iconUrl, plan.siteDomain, plan.fetcher,
                                      plan.pageFetcher, bytes);

        lock.lock();
        --loader->inFlight;
        if (!loader->owner) { --loader->threads; return; }
        SenderIconCache::ReadyHandler ready;
        const std::string stored = loader->owner->Finish(key, fetched, bytes, &ready);
        // Still under the loader's lock, so the cache (and whoever set the
        // handler) is alive while it runs; it only posts to the UI thread.
        if (!stored.empty() && ready) ready(key);
    }
}

SenderIconCache::SenderIconCache() : loader_(std::make_shared<Loader>()) {
    loader_->owner = this;
}

SenderIconCache::~SenderIconCache() {
    std::lock_guard<std::mutex> lock(loader_->mutex);
    loader_->owner = nullptr;
    loader_->queue.clear();
    loader_->wake.notify_all();
}

void SenderIconCache::Request(const std::string& key) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ValidKey(key) || !network_ || !fetcher_ || root_.empty()) return;
        if (IsSiteKey(key) && !siteIcons_) return;
        if (!PathFor(key).empty()) return;              // cached already
        if (!requested_.insert(key).second) return;     // asked for this session
    }
    std::lock_guard<std::mutex> lock(loader_->mutex);
    if (!loader_->owner) return;
    loader_->queue.push_back(key);
    if (loader_->idle > 0) {
        loader_->wake.notify_one();
    } else if (loader_->threads < kMaxLoaders) {
        ++loader_->threads;
        std::thread(RunIconLoader, loader_).detach();
    }
}

std::size_t SenderIconCache::PendingCount() const {
    std::lock_guard<std::mutex> lock(loader_->mutex);
    return loader_->queue.size() + static_cast<std::size_t>(loader_->inFlight);
}

SenderIconCache::FetchPlan SenderIconCache::PlanFetch(const std::string& key) {
    FetchPlan plan;
    std::lock_guard<std::mutex> lock(mutex_);
    if (root_.empty() || !network_ || !fetcher_ || !ValidKey(key)) return plan;
    if (!PathFor(key).empty() || !RetryAllowed(key)) return plan;
    if (IsSiteKey(key)) {
        if (!siteIcons_) return plan;
        plan.siteDomain  = key.substr(5);
        plan.pageFetcher = pageFetcher_;
    } else {
        const SenderBrand* brand = BrandById(key);
        if (!brand || brand->iconUrl.empty()) return plan;
        plan.iconUrl = brand->iconUrl;
    }
    plan.fetcher = fetcher_;
    return plan;
}

std::string SenderIconCache::Finish(const std::string& key, bool fetched,
                                    const std::vector<uint8_t>& bytes, ReadyHandler* ready) {
    const std::string stored = fetched ? Store(key, bytes) : std::string();
    std::lock_guard<std::mutex> lock(mutex_);
    if (stored.empty()) NoteFailure(key);   // a body that is not an image is a failure too
    if (ready) *ready = ready_;
    return stored;
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
void SenderIconCache::SetRoot(std::string directory) {
    std::lock_guard<std::mutex> lock(mutex_);
    root_ = std::move(directory);
    resolved_.clear();
    primed_ = false;
}

std::string SenderIconCache::Root() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return root_;
}

void SenderIconCache::SetFetcher(Fetcher fetcher) {
    std::lock_guard<std::mutex> lock(mutex_);
    fetcher_ = std::move(fetcher);
}

void SenderIconCache::SetPageFetcher(PageFetcher fetcher) {
    std::lock_guard<std::mutex> lock(mutex_);
    pageFetcher_ = std::move(fetcher);
}

void SenderIconCache::SetReadyHandler(ReadyHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    ready_ = std::move(handler);
}

void SenderIconCache::SetNetworkEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (enabled && !network_) requested_.clear();   // everything may be asked for again
    network_ = enabled;
}

bool SenderIconCache::NetworkEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return network_;
}

void SenderIconCache::SetSiteIconsEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    siteIcons_ = enabled;
}

bool SenderIconCache::SiteIconsEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return siteIcons_;
}

void SenderIconCache::SetRetryInterval(int64_t seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    retrySeconds_ = seconds;
}

// ---------------------------------------------------------------------------
// The folder
// ---------------------------------------------------------------------------
// The folder a key's files live in, and their name without the extension.
static void Locate(const std::string& root, const std::string& key,
                   fs::path& folder, std::string& name) {
    folder = PathFromUtf8(root);
    if (IsSiteKey(key)) {
        folder /= kSiteFolder;
        name = key.substr(5);
    } else {
        name = key;
    }
}

// Every icon in the folder (and in sites/) into resolved_, in one listing of
// each; where a key has files of two kinds, the earlier in kExtensions wins.
void SenderIconCache::Prime() const {
    if (primed_ || root_.empty()) return;
    primed_ = true;
    std::map<std::string, std::size_t> rank;
    auto scan = [this, &rank](const fs::path& folder, const std::string& prefix) {
        std::error_code ec;
        for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
            const fs::path& p = it->path();
            std::string ext = Lower(PathToUtf8(p.extension()));
            if (ext.size() < 2) continue;
            ext.erase(0, 1);
            const std::size_t at = static_cast<std::size_t>(
                std::find_if(std::begin(kExtensions), std::end(kExtensions),
                             [&ext](const char* e) { return ext == e; }) -
                std::begin(kExtensions));
            if (at == std::size(kExtensions)) continue;   // .missing, .part
            const std::string key = prefix + PathToUtf8(p.stem());
            if (!ValidKey(key)) continue;
            if (const auto r = rank.find(key); r != rank.end() && r->second <= at) continue;
            rank[key] = at;
            resolved_[key] = PathToUtf8(p);
        }
    };
    scan(PathFromUtf8(root_), "");
    scan(PathFromUtf8(root_) / kSiteFolder, kSitePrefix);
}

std::string SenderIconCache::PathFor(const std::string& key) const {
    if (root_.empty() || !ValidKey(key)) return "";
    Prime();
    if (const auto memo = resolved_.find(key); memo != resolved_.end())
        return memo->second;
    return "";   // not in the folder; Store() adds it when it is
}

std::string SenderIconCache::IconForKey(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return PathFor(key);
}

std::string SenderIconCache::IconForBrand(const std::string& brandId) const {
    return IconForKey(brandId);
}

std::string SenderIconCache::IconForAddress(const std::string& address) const {
    const SenderBrand* brand = BrandForAddress(address);
    return brand ? IconForBrand(brand->id) : std::string();
}

bool SenderIconCache::RetryAllowed(const std::string& key) const {
    if (root_.empty()) return false;
    fs::path folder;
    std::string name;
    Locate(root_, key, folder, name);
    const fs::path marker = folder / PathFromUtf8(name + ".missing");
    std::error_code ec;
    if (!fs::exists(marker, ec)) return true;
    std::ifstream in(marker);
    int64_t when = 0;
    in >> when;
    return Now() - when >= retrySeconds_;
}

void SenderIconCache::NoteFailure(const std::string& key) {
    if (root_.empty() || !ValidKey(key)) return;
    fs::path folder;
    std::string name;
    Locate(root_, key, folder, name);
    std::error_code ec;
    fs::create_directories(folder, ec);
    std::ofstream out(folder / PathFromUtf8(name + ".missing"), std::ios::trunc);
    if (out) out << Now() << "\n";
}

std::string SenderIconCache::Store(const std::string& key,
                                   const std::vector<uint8_t>& bytes) {
    const std::string ext = SniffImageExtension(bytes);
    std::lock_guard<std::mutex> lock(mutex_);
    if (root_.empty() || !ValidKey(key) || ext.empty()) return "";

    fs::path folder;
    std::string name;
    Locate(root_, key, folder, name);
    std::error_code ec;
    fs::create_directories(folder, ec);
    const fs::path path = folder / PathFromUtf8(name + "." + ext);
    // Write beside the target and rename, so a half-written icon is never seen
    // by the UI thread reading the same folder.
    const fs::path temp = folder / PathFromUtf8(name + ".part");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return "";
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        if (!out) return "";
    }
    fs::rename(temp, path, ec);
    if (ec) { fs::remove(temp, ec); return ""; }
    fs::remove(folder / PathFromUtf8(name + ".missing"), ec);
    resolved_[key] = PathToUtf8(path);
    return PathToUtf8(path);
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
