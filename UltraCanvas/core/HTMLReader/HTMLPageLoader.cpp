// core/HTMLReader/HTMLPageLoader.cpp
// URL -> decoded, parsed page with its style sheets; subresources on demand.
// See include/HTMLReader/HTMLPageLoader.h.
// Version: 1.3.0 - downloads (LoadedPage body / fileName, DownloadFileName)
// Version: 1.2.0 - LoadPost: a form's POST
// Version: 1.1.0 - @import sheets fetched before the page is ready
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "HTMLReader/HTMLPageLoader.h"
#include "HTMLReader/CSSStyleSheet.h"
#include "HTMLReader/HTMLParser.h"
#include "UltraCanvasTextUtils.h"

#include <algorithm>
#include <cctype>

namespace UltraCanvas {
namespace HTML {

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The scheme of an absolute URL, lower-case; "" when there is none.
std::string SchemeOf(const std::string& url) {
    const size_t colon = url.find(':');
    if (colon == std::string::npos || colon == 0) return std::string();
    for (size_t i = 0; i < colon; ++i) {
        const unsigned char c = static_cast<unsigned char>(url[i]);
        if (!std::isalnum(c) && c != '+' && c != '-' && c != '.') return std::string();
    }
    return Lower(url.substr(0, colon));
}

// The media type of a Content-Type value, lower-case, without parameters.
std::string MediaType(const std::string& contentType) {
    const size_t semi = contentType.find(';');
    return Lower(Trim(contentType.substr(0, semi)));
}

// The charset parameter of a Content-Type value ("" when it has none).
std::string CharsetParam(const std::string& contentType) {
    const std::string lower = Lower(contentType);
    size_t at = lower.find("charset=");
    if (at == std::string::npos) return std::string();
    at += 8;
    std::string value;
    if (at < lower.size() && (lower[at] == '"' || lower[at] == '\'')) {
        const char quote = lower[at++];
        const size_t end = lower.find(quote, at);
        value = lower.substr(at, end == std::string::npos ? std::string::npos : end - at);
    } else {
        size_t end = at;
        while (end < lower.size() && lower[end] != ';' && lower[end] != ' ' && lower[end] != '"' &&
               lower[end] != '\'' && lower[end] != '>' && lower[end] != '/')
            ++end;
        value = lower.substr(at, end - at);
    }
    return Trim(value);
}

// The charset a <meta> in the first 1024 bytes names: <meta charset="x"> or
// <meta http-equiv="content-type" content="text/html; charset=x">.
std::string MetaCharset(const std::vector<uint8_t>& bytes) {
    const std::string head = Lower(std::string(bytes.begin(),
                                               bytes.begin() + static_cast<std::ptrdiff_t>(
                                                   std::min<size_t>(bytes.size(), 1024))));
    size_t from = 0;
    while (true) {
        const size_t meta = head.find("<meta", from);
        if (meta == std::string::npos) return std::string();
        const size_t end = head.find('>', meta);
        const std::string tag = head.substr(meta, end == std::string::npos ? std::string::npos : end - meta + 1);
        const std::string cs = CharsetParam(tag);
        if (!cs.empty()) return cs;
        if (end == std::string::npos) return std::string();
        from = end;
    }
}

int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string WithoutFragment(const std::string& url) {
    const size_t hash = url.find('#');
    return hash == std::string::npos ? url : url.substr(0, hash);
}

} // namespace

struct PageLoader::Navigation {
    uint64_t id = 0;
    std::function<void(LoadedPage)> onReady;
    LoadedPage page;
    size_t sheetsPending = 0;
    std::vector<std::string> sheetUrls;   // every sheet asked for, links and imports
    bool sheetsQueued = false;            // all of the page's own sheets are asked for
};

namespace {
constexpr int kMaxImportDepth = 4;
}

PageLoader::PageLoader(PageLoaderServices services, PageLoaderLimits limits)
    : services_(std::move(services)), limits_(limits), generation_(std::make_shared<uint64_t>(0)) {}

PageLoader::~PageLoader() = default;

bool PageLoader::IsLoading() const { return current_ != nullptr; }
size_t PageLoader::PendingResources() const { return pending_; }
const std::string& PageLoader::BaseUrl() const { return baseUrl_; }

uint64_t PageLoader::LoadPost(const std::string& url, const std::string& contentType,
                              const std::string& body, std::function<void(LoadedPage)> onReady) {
    if (!services_.post) {
        Cancel();
        LoadedPage page;
        page.url = url;
        page.error = "this browser cannot send the form (no way to post)";
        onReady(std::move(page));
        return *generation_;
    }
    PostRequest request{ contentType, body };
    return Start(url, std::move(onReady), &request);
}

uint64_t PageLoader::Load(const std::string& url, std::function<void(LoadedPage)> onReady) {
    return Start(url, std::move(onReady), nullptr);
}

uint64_t PageLoader::Start(const std::string& url, std::function<void(LoadedPage)> onReady,
                           const PostRequest* post) {
    Cancel();
    auto nav = std::make_shared<Navigation>();
    nav->id = *generation_;
    nav->onReady = std::move(onReady);
    nav->page.url = url;
    current_ = nav;
    if (!services_.fetch && !post) {
        LoadedPage page;
        page.url = url;
        page.error = "no way to fetch pages";
        current_.reset();
        nav->onReady(std::move(page));
        return nav->id;
    }
    std::weak_ptr<uint64_t> alive = generation_;
    const uint64_t id = nav->id;
    // A #fragment is never sent: it names a place in the page, and stays
    // with the page's address (across a redirect too, as in browsers).
    const size_t hash = url.find('#');
    const std::string fragment = hash == std::string::npos ? std::string() : url.substr(hash);
    auto done = [this, alive, id, nav, fragment](FetchResult result) {
        auto gen = alive.lock();
        if (!gen || *gen != id) return;           // left, or the loader is gone
        if (!fragment.empty() && !result.finalUrl.empty() && result.finalUrl.find('#') == std::string::npos)
            result.finalUrl += fragment;
        Finish(nav, std::move(result));
    };
    if (post) services_.post(WithoutFragment(url), post->contentType, post->body, std::move(done));
    else services_.fetch(WithoutFragment(url), std::move(done));
    return id;
}

void PageLoader::Cancel() {
    ++*generation_;
    current_.reset();
    pending_ = 0;
    // Fetches still under way belong to the page being left: their answers
    // will be dropped, so their entries must not stay "asked for" forever.
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (it->second.state == CacheEntry::State::Pending) {
            cacheOrder_.erase(std::remove(cacheOrder_.begin(), cacheOrder_.end(), it->first),
                              cacheOrder_.end());
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

void PageLoader::Finish(const std::shared_ptr<Navigation>& nav, FetchResult result) {
    LoadedPage& page = nav->page;
    if (!result.finalUrl.empty()) page.url = result.finalUrl;
    page.status = result.status;
    page.contentType = result.contentType;
    auto fail = [&](const std::string& why) {
        page.kind = PageKind::Error;
        page.error = why;
        current_.reset();
        nav->onReady(std::move(page));
    };
    const std::string mediaType = MediaType(result.contentType);
    const bool attachment = Lower(Trim(result.contentDisposition)).rfind("attachment", 0) == 0;
    const bool showable = mediaType.empty() || mediaType == "text/html" || mediaType == "application/xhtml+xml" ||
                          mediaType == "text/plain" || mediaType.rfind("image/", 0) == 0;
    // A download: the server says to save it, or it is no page and over the
    // limit (the host fetches it again, streaming, to disk).
    auto download = [&](bool truncated) {
        page.kind = PageKind::Other;
        page.attachment = attachment;
        page.truncated = truncated;
        page.fileName = DownloadFileName(result.contentDisposition, page.url, result.contentType);
        if (!truncated) page.body = std::move(result.body);
        current_.reset();
        nav->onReady(std::move(page));
    };
    if (!result.ok) {
        if (result.tooLarge && (attachment || !showable)) { download(true); return; }
        fail(result.error.empty() ? std::string("the page could not be loaded") : result.error);
        return;
    }
    if (result.body.size() > limits_.maxDocumentBytes) {
        if (attachment || !showable) { download(true); return; }
        fail("the page is larger than " + std::to_string(limits_.maxDocumentBytes / (1024 * 1024)) + " MB");
        return;
    }
    if (attachment) { download(false); return; }

    // What it is: the media type, else a guess from the bytes (file: has none).
    std::string type = MediaType(result.contentType);
    if (type.empty()) {
        const std::string path = Lower(WithoutFragment(page.url));
        auto endsWith = [&](const char* ext) {
            const std::string e(ext);
            return path.size() >= e.size() && path.compare(path.size() - e.size(), e.size(), e) == 0;
        };
        if (endsWith(".html") || endsWith(".htm") || endsWith(".xhtml")) type = "text/html";
        else if (endsWith(".txt") || endsWith(".md")) type = "text/plain";
        else if (endsWith(".png") || endsWith(".jpg") || endsWith(".jpeg") || endsWith(".gif") ||
                 endsWith(".webp") || endsWith(".svg") || endsWith(".bmp")) type = "image/*";
        else {
            // Markup at the start reads as HTML, as browsers sniff.
            size_t i = 0;
            while (i < result.body.size() && std::isspace(result.body[i])) ++i;
            if (i < result.body.size() && result.body[i] == '<') type = "text/html";
        }
    }

    // A file:// URL is its own base; a fragment never is part of one.
    pageUrl_ = page.url;
    baseUrl_ = WithoutFragment(page.url);
    requested_ = 0;

    std::string html;
    if (type == "text/html" || type == "application/xhtml+xml") {
        page.kind = PageKind::Html;
        page.charset = DetectCharset(result.body, result.contentType);
        html = ToUtf8(result.body, page.charset);
    } else if (type == "text/plain") {
        page.kind = PageKind::Text;
        page.charset = DetectCharset(result.body, result.contentType);
        html = "<!DOCTYPE html><html><head><title>" + EscapeText(page.url) +
               "</title></head><body><pre style='white-space:pre-wrap'>" +
               EscapeText(ToUtf8(result.body, page.charset)) + "</pre></body></html>";
    } else if (type.rfind("image/", 0) == 0) {
        page.kind = PageKind::Image;
        // The picture is the page's only resource, and already here.
        CacheEntry entry;
        entry.state = CacheEntry::State::Done;
        entry.bytes = std::move(result.body);
        Store(WithoutFragment(page.url), std::move(entry));
        html = "<!DOCTYPE html><html><head><title>" + EscapeText(page.url) +
               "</title></head><body style='margin:0;text-align:center'><img src=\"" +
               EscapeText(page.url) + "\" style='max-width:100%'></body></html>";
    } else {
        download(false);
        return;
    }

    Parser parser;
    page.document = std::make_shared<Document>(parser.Parse(html));
    // <base href>: the first one with an href, read against the page's URL.
    if (page.document->root && page.kind == PageKind::Html) {
        page.document->root->ForEachElement([&](Node& n) {
            if (n.tag != "base" || !n.HasAttribute("href")) return true;
            std::string absolute;
            if (services_.resolve && services_.resolve(baseUrl_, Trim(n.GetAttribute("href")), absolute))
                baseUrl_ = absolute;
            return false;
        });
    }
    page.baseUrl = baseUrl_;
    FetchStyleSheets(nav);
}

void PageLoader::FetchStyleSheets(const std::shared_ptr<Navigation>& nav) {
    LoadedPage& page = nav->page;
    // Counted pending while the page's own sheets are queued, so a sheet
    // answered at once does not make the page ready before the rest are asked for.
    ++nav->sheetsPending;
    for (const std::string& href : page.document->styleSheetLinks) {
        const std::string absolute = Resolve(href);
        std::string why;
        if (absolute.empty() || !Allowed(absolute, why)) {
            page.warnings.push_back("style sheet not loaded: " + href + (why.empty() ? "" : " (" + why + ")"));
            continue;
        }
        FetchSheet(nav, absolute, 0);
    }
    // The sheets a <style> imports, read against the page's base.
    for (const std::string& css : page.document->styleSheets) FetchImports(nav, css, baseUrl_, 0);
    nav->sheetsQueued = true;
    SheetDone(nav);
}

void PageLoader::SheetDone(const std::shared_ptr<Navigation>& nav) {
    if (--nav->sheetsPending != 0 || !nav->sheetsQueued) return;
    if (current_ != nav) return;
    current_.reset();
    nav->onReady(std::move(nav->page));
}

void PageLoader::FetchImports(const std::shared_ptr<Navigation>& nav, const std::string& css,
                              const std::string& sheetUrl, int depth) {
    for (const StyleImport& import : StyleSheet::ParseImports(css)) {
        std::string absolute;
        if (!services_.resolve || !services_.resolve(sheetUrl, import.href, absolute)) absolute = import.href;
        std::string why;
        if (depth >= kMaxImportDepth) {
            nav->page.warnings.push_back("style sheet not loaded: " + absolute + " (imports nested too deep)");
            continue;
        }
        if (!Allowed(absolute, why)) {
            nav->page.warnings.push_back("style sheet not loaded: " + absolute + (why.empty() ? "" : " (" + why + ")"));
            continue;
        }
        FetchSheet(nav, absolute, depth + 1);
    }
}

void PageLoader::FetchSheet(const std::shared_ptr<Navigation>& nav, const std::string& url, int depth) {
    const std::string key = WithoutFragment(url);
    if (std::find(nav->sheetUrls.begin(), nav->sheetUrls.end(), key) != nav->sheetUrls.end()) return;   // once
    if (nav->sheetUrls.size() >= limits_.maxStyleSheets) {
        if (nav->sheetUrls.size() == limits_.maxStyleSheets) {
            nav->page.warnings.push_back("more than " + std::to_string(limits_.maxStyleSheets) +
                                         " style sheets: the rest are not loaded");
            nav->sheetUrls.push_back(std::string());   // warn once
        }
        return;
    }
    nav->sheetUrls.push_back(key);
    ++nav->sheetsPending;
    if (key.rfind("data:", 0) == 0) {
        CacheEntry entry;
        entry.state = DecodeDataUrl(key, entry.bytes) ? CacheEntry::State::Done : CacheEntry::State::Failed;
        const std::string css(entry.bytes.begin(), entry.bytes.end());
        Store(key, std::move(entry));
        FetchImports(nav, css, baseUrl_, depth);
        SheetDone(nav);
        return;
    }
    std::weak_ptr<uint64_t> alive = generation_;
    const uint64_t id = nav->id;
    services_.fetch(key, [this, alive, id, nav, key, depth](FetchResult result) {
        auto gen = alive.lock();
        if (!gen || *gen != id) return;
        CacheEntry entry;
        std::string rewritten;
        std::string sheetUrl = key;
        if (result.ok && result.status < 400) {
            if (!result.finalUrl.empty()) sheetUrl = result.finalUrl;
            const std::string charset = CharsetParam(result.contentType);
            const std::string css = ToUtf8(result.body, charset.empty() ? "utf-8" : charset);
            rewritten = services_.resolve ? RewriteCssUrls(css, sheetUrl, services_.resolve) : css;
            entry.state = CacheEntry::State::Done;
            entry.bytes.assign(rewritten.begin(), rewritten.end());
        } else {
            entry.state = CacheEntry::State::Failed;
            nav->page.warnings.push_back("style sheet not loaded: " + key +
                                         (result.error.empty() ? "" : " (" + result.error + ")"));
        }
        Store(key, std::move(entry));
        if (!rewritten.empty()) FetchImports(nav, rewritten, sheetUrl, depth);
        SheetDone(nav);
    });
}

std::vector<uint8_t> PageLoader::GetResource(const std::string& href) {
    const std::string absolute = Resolve(href);
    if (absolute.empty()) return {};
    const std::string key = WithoutFragment(absolute);
    if (auto it = cache_.find(key); it != cache_.end()) {
        return it->second.state == CacheEntry::State::Done ? it->second.bytes : std::vector<uint8_t>{};
    }
    if (key.rfind("data:", 0) == 0) {
        CacheEntry entry;
        entry.state = DecodeDataUrl(key, entry.bytes) ? CacheEntry::State::Done : CacheEntry::State::Failed;
        std::vector<uint8_t> bytes = entry.bytes;
        Store(key, std::move(entry));
        return bytes;
    }
    std::string why;
    if (!Allowed(key, why) || !services_.fetch || requested_ >= limits_.maxResources) {
        CacheEntry failed;
        failed.state = CacheEntry::State::Failed;
        Store(key, std::move(failed));
        return {};
    }
    ++requested_;
    ++pending_;
    Store(key, CacheEntry{});              // pending: asked for once
    std::weak_ptr<uint64_t> alive = generation_;
    const uint64_t id = *generation_;
    services_.fetch(key, [this, alive, id, key](FetchResult result) {
        auto gen = alive.lock();
        if (!gen || *gen != id) return;
        CacheEntry entry;
        entry.state = (result.ok && result.status < 400 && !result.body.empty())
                          ? CacheEntry::State::Done : CacheEntry::State::Failed;
        if (entry.state == CacheEntry::State::Done) entry.bytes = std::move(result.body);
        Store(key, std::move(entry));
        if (pending_ > 0) --pending_;
        if (onResourcesArrived) onResourcesArrived();
    });
    return {};
}

std::string PageLoader::Resolve(const std::string& href) const {
    const std::string ref = Trim(href);
    if (ref.rfind("data:", 0) == 0) return ref;
    if (baseUrl_.empty() || !services_.resolve) return std::string();
    std::string absolute;
    if (!services_.resolve(baseUrl_, ref, absolute)) return std::string();
    return absolute;
}

bool PageLoader::Allowed(const std::string& absolute, std::string& why) const {
    const std::string scheme = SchemeOf(absolute);
    const std::string pageScheme = SchemeOf(pageUrl_);
    if (scheme == "data") return true;
    if (scheme == "https") return true;
    if (scheme == "http") {
        if (pageScheme == "https") { why = "http on an https page"; return false; }
        return true;
    }
    if (scheme == "file") {
        if (pageScheme == "file") return true;
        why = "a web page cannot read local files";
        return false;
    }
    why = scheme.empty() ? "not a URL" : "scheme " + scheme;
    return false;
}

void PageLoader::Store(const std::string& url, CacheEntry entry) {
    auto it = cache_.find(url);
    if (it != cache_.end()) {
        cacheBytes_ -= std::min(cacheBytes_, it->second.bytes.size());
        cacheBytes_ += entry.bytes.size();
        it->second = std::move(entry);
    } else {
        cacheBytes_ += entry.bytes.size();
        cache_.emplace(url, std::move(entry));
        cacheOrder_.push_back(url);
    }
    // Oldest out first while over budget; a pending entry stays (its fetch
    // would only queue it again).
    size_t guard = cacheOrder_.size();
    while (cacheBytes_ > limits_.cacheBytes && !cacheOrder_.empty() && guard-- > 0) {
        const std::string oldest = cacheOrder_.front();
        cacheOrder_.pop_front();
        auto old = cache_.find(oldest);
        if (old == cache_.end()) continue;
        if (old->second.state == CacheEntry::State::Pending || oldest == url) {
            cacheOrder_.push_back(oldest);
            continue;
        }
        cacheBytes_ -= std::min(cacheBytes_, old->second.bytes.size());
        cache_.erase(old);
    }
}

std::string PageLoader::ToUtf8(const std::vector<uint8_t>& bytes, const std::string& charset) const {
    std::string text(bytes.begin(), bytes.end());
    // The BOM is not content.
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        text.erase(0, 3);
    const std::string cs = Lower(charset);
    if (cs.empty() || cs == "utf-8" || cs == "utf8") {
        if (IsWellFormedUtf8(text)) return text;
    }
    if (services_.toUtf8) return services_.toUtf8(text, cs.empty() ? "utf-8" : cs);
    if (IsWellFormedUtf8(text)) return text;
    // Without a converter: read the bytes as Latin-1 (which Windows-1252
    // pages mostly are), so the text is valid UTF-8 at least.
    std::string out;
    out.reserve(text.size() + text.size() / 4);
    for (unsigned char c : text) {
        if (c < 0x80) out += static_cast<char>(c);
        else {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

std::string PageLoader::DetectCharset(const std::vector<uint8_t>& bytes, const std::string& contentType) {
    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) return "utf-8";
    if (bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) return "utf-16be";
    if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) return "utf-16le";
    if (std::string cs = CharsetParam(contentType); !cs.empty()) return cs;
    if (std::string cs = MetaCharset(bytes); !cs.empty()) {
        // A page that says utf-16 in ASCII bytes is not utf-16 (HTML spec).
        if (cs.rfind("utf-16", 0) == 0) return "utf-8";
        return cs;
    }
    return IsWellFormedUtf8(std::string(bytes.begin(), bytes.end())) ? "utf-8" : "windows-1252";
}

std::string PageLoader::RewriteCssUrls(
    const std::string& css, const std::string& sheetUrl,
    const std::function<bool(const std::string&, const std::string&, std::string&)>& resolve) {
    std::string out;
    out.reserve(css.size());
    size_t i = 0;
    while (i < css.size()) {
        // Comments pass through untouched.
        if (css.compare(i, 2, "/*") == 0) {
            const size_t end = css.find("*/", i + 2);
            const size_t stop = end == std::string::npos ? css.size() : end + 2;
            out.append(css, i, stop - i);
            i = stop;
            continue;
        }
        // @import "a.css": a string, not url(), names the sheet.
        if (css[i] == '@' && i + 7 <= css.size() && Lower(css.substr(i + 1, 6)) == "import" &&
            (i + 7 == css.size() || !(std::isalnum(static_cast<unsigned char>(css[i + 7])) || css[i + 7] == '-'))) {
            size_t j = i + 7;
            while (j < css.size() && std::isspace(static_cast<unsigned char>(css[j]))) ++j;
            if (j < css.size() && (css[j] == '"' || css[j] == '\'')) {
                const char quote = css[j];
                size_t k = j + 1;
                std::string ref;
                while (k < css.size() && css[k] != quote) {
                    if (css[k] == '\\' && k + 1 < css.size()) ++k;
                    ref += css[k++];
                }
                if (k < css.size()) {
                    std::string absolute;
                    if (ref.rfind("data:", 0) == 0 || ref.empty() || !resolve(sheetUrl, ref, absolute)) absolute = ref;
                    out += "@import url(\"";
                    for (char c : absolute) {
                        if (c == '"' || c == '\\') out += '\\';
                        out += c;
                    }
                    out += "\")";
                    i = k + 1;
                    continue;
                }
            }
        }
        const bool isUrl = i + 4 <= css.size() &&
                           std::tolower(static_cast<unsigned char>(css[i])) == 'u' &&
                           std::tolower(static_cast<unsigned char>(css[i + 1])) == 'r' &&
                           std::tolower(static_cast<unsigned char>(css[i + 2])) == 'l' &&
                           css[i + 3] == '(' &&
                           (i == 0 || !(std::isalnum(static_cast<unsigned char>(css[i - 1])) || css[i - 1] == '-'));
        if (!isUrl) { out += css[i++]; continue; }
        size_t j = i + 4;
        while (j < css.size() && std::isspace(static_cast<unsigned char>(css[j]))) ++j;
        char quote = 0;
        if (j < css.size() && (css[j] == '"' || css[j] == '\'')) quote = css[j++];
        const size_t start = j;
        while (j < css.size() && (quote ? css[j] != quote : (css[j] != ')' && !std::isspace(static_cast<unsigned char>(css[j]))))) {
            if (css[j] == '\\' && j + 1 < css.size()) ++j;
            ++j;
        }
        const std::string ref = css.substr(start, j - start);
        if (quote && j < css.size()) ++j;
        while (j < css.size() && std::isspace(static_cast<unsigned char>(css[j]))) ++j;
        if (j >= css.size() || css[j] != ')') {       // not a url() after all
            out += css[i++];
            continue;
        }
        std::string absolute;
        if (ref.rfind("data:", 0) == 0 || ref.empty() || !resolve(sheetUrl, ref, absolute)) absolute = ref;
        out += "url(\"";
        for (char c : absolute) {
            if (c == '"' || c == '\\') out += '\\';
            out += c;
        }
        out += "\")";
        i = j + 1;
    }
    return out;
}

std::string PageLoader::DownloadFileName(const std::string& contentDisposition, const std::string& url,
                                         const std::string& contentType) {
    auto percentDecode = [](const std::string& s) {
        std::string out;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '%' && i + 2 < s.size() && HexValue(s[i + 1]) >= 0 && HexValue(s[i + 2]) >= 0) {
                out += static_cast<char>(HexValue(s[i + 1]) * 16 + HexValue(s[i + 2]));
                i += 2;
            } else {
                out += s[i];
            }
        }
        return out;
    };
    // A parameter of the header: name=value, name="quoted value".
    auto param = [&](const std::string& name) -> std::string {
        const std::string lower = Lower(contentDisposition);
        size_t at = 0;
        while ((at = lower.find(name, at)) != std::string::npos) {
            const bool starts = at == 0 || lower[at - 1] == ';' || std::isspace(static_cast<unsigned char>(lower[at - 1]));
            size_t eq = at + name.size();
            while (eq < lower.size() && std::isspace(static_cast<unsigned char>(lower[eq]))) ++eq;
            if (!starts || eq >= lower.size() || lower[eq] != '=') { at += name.size(); continue; }
            size_t v = eq + 1;
            while (v < contentDisposition.size() && std::isspace(static_cast<unsigned char>(contentDisposition[v]))) ++v;
            std::string value;
            if (v < contentDisposition.size() && contentDisposition[v] == '"') {
                for (++v; v < contentDisposition.size() && contentDisposition[v] != '"'; ++v) {
                    if (contentDisposition[v] == '\\' && v + 1 < contentDisposition.size()) ++v;
                    value += contentDisposition[v];
                }
            } else {
                const size_t end = contentDisposition.find(';', v);
                value = Trim(contentDisposition.substr(v, end == std::string::npos ? std::string::npos : end - v));
            }
            return value;
        }
        return std::string();
    };
    std::string name;
    // filename*=UTF-8''caf%C3%A9.pdf wins over filename=.
    const std::string extended = param("filename*");
    if (!extended.empty()) {
        const size_t quote = extended.find("''");
        if (quote != std::string::npos) {
            const std::string charset = Lower(extended.substr(0, quote));
            if (charset == "utf-8" || charset.empty()) name = percentDecode(extended.substr(quote + 2));
        }
    }
    if (name.empty()) name = param("filename");
    if (name.empty()) {
        std::string path = WithoutFragment(url);
        path = path.substr(0, path.find('?'));
        const size_t scheme = path.find("://");
        const size_t start = scheme == std::string::npos ? 0 : path.find('/', scheme + 3);
        if (start != std::string::npos) {
            const size_t slash = path.rfind('/');
            if (slash != std::string::npos && slash >= start) name = percentDecode(path.substr(slash + 1));
        }
    }
    // Only a name: no folders, control characters or names that hide.
    std::string clean;
    for (unsigned char c : name) {
        if (c < 0x20 || c == 0x7F) continue;
        clean += (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
                  c == '<' || c == '>' || c == '|') ? '_' : static_cast<char>(c);
    }
    clean = Trim(clean);
    while (!clean.empty() && clean[0] == '.') clean.erase(0, 1);
    if (clean.size() > 200) clean = clean.substr(clean.size() - 200);
    if (!clean.empty()) return clean;
    static const std::pair<const char*, const char*> kExtensions[] = {
        { "application/pdf", ".pdf" }, { "application/zip", ".zip" }, { "application/gzip", ".gz" },
        { "application/json", ".json" }, { "text/csv", ".csv" }, { "text/plain", ".txt" },
        { "text/html", ".html" }, { "image/png", ".png" }, { "image/jpeg", ".jpg" },
        { "audio/mpeg", ".mp3" }, { "video/mp4", ".mp4" }, { "application/wasm", ".wasm" },
    };
    const std::string type = MediaType(contentType);
    for (const auto& [t, ext] : kExtensions)
        if (type == t) return std::string("download") + ext;
    return "download";
}

bool PageLoader::DecodeDataUrl(const std::string& url, std::vector<uint8_t>& out) {
    out.clear();
    if (url.size() < 5 || Lower(url.substr(0, 5)) != "data:") return false;
    const size_t comma = url.find(',');
    if (comma == std::string::npos) return false;
    const std::string meta = Lower(url.substr(5, comma - 5));
    const std::string payload = url.substr(comma + 1);
    const bool base64 = meta.size() >= 7 && meta.compare(meta.size() - 7, 7, ";base64") == 0;
    if (base64) {
        std::string compact;
        compact.reserve(payload.size());
        for (size_t i = 0; i < payload.size(); ++i) {
            // Percent-escapes inside base64 (%2B, %3D) happen in hand-made URLs.
            if (payload[i] == '%' && i + 2 < payload.size() && HexValue(payload[i + 1]) >= 0 &&
                HexValue(payload[i + 2]) >= 0) {
                compact += static_cast<char>(HexValue(payload[i + 1]) * 16 + HexValue(payload[i + 2]));
                i += 2;
            } else if (!std::isspace(static_cast<unsigned char>(payload[i]))) {
                compact += payload[i];
            }
        }
        out = Base64Decode(compact);
        return !out.empty() || compact.empty();
    }
    out.reserve(payload.size());
    for (size_t i = 0; i < payload.size(); ++i) {
        if (payload[i] == '%' && i + 2 < payload.size() && HexValue(payload[i + 1]) >= 0 &&
            HexValue(payload[i + 2]) >= 0) {
            out.push_back(static_cast<uint8_t>(HexValue(payload[i + 1]) * 16 + HexValue(payload[i + 2])));
            i += 2;
        } else {
            out.push_back(static_cast<uint8_t>(payload[i]));
        }
    }
    return true;
}

} // namespace HTML
} // namespace UltraCanvas
