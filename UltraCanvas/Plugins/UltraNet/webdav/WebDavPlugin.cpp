// UltraCanvas/Plugins/UltraNet/webdav/WebDavPlugin.cpp
// WebDAV / WebDAVS plug-in. Implements IFileShareProtocolPlugin on top of
// libcurl's HTTP transport using WebDAV's PROPFIND / MKCOL / MOVE / COPY
// methods (RFC 4918). Treats webdav:// as http:// and webdavs:// as https://
// when issuing libcurl requests.
//
// Build: produces libultranet_webdav.{so,dylib,dll}.
// Entry points: UltraNet_PluginInit (v2) and UltraNet_PluginRegister (v1).
// Version: 0.1.2
// Last Modified: 2026-07-05
// Author: UltraCanvas Framework / ULTRA OS

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetFtp.h>
#include <UltraNet/UltraNetPlugins.h>
#include <UltraNet/UltraNetUrl.h>

#include <curl/curl.h>

// On Windows, <curl/curl.h> pulls in <winsock2.h> -> <windows.h>, which #defines
// CreateDirectory as CreateDirectoryA/W. That macro would textually rewrite our
// IFileShareProtocolPlugin::CreateDirectory override (below) into CreateDirectoryA,
// so it no longer matches the base vtable slot and WebDavPlugin stays abstract.
// This plug-in speaks WebDAV over libcurl (MKCOL), not the Win32 filesystem API,
// so drop the macro. (#undef of an undefined macro is a harmless no-op elsewhere.)
#ifdef CreateDirectory
#  undef CreateDirectory
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <regex>
#include <string>
#include <utility>
#include <vector>

namespace {

// ===== Helpers ===============================================================

std::size_t WriteString(char* p, std::size_t s, std::size_t n, void* ud) {
    static_cast<std::string*>(ud)->append(p, s * n);
    return s * n;
}
std::size_t WriteFile(char* p, std::size_t s, std::size_t n, void* ud) {
    return std::fwrite(p, 1, s * n, static_cast<std::FILE*>(ud));
}
std::size_t ReadFile(char* p, std::size_t s, std::size_t n, void* ud) {
    return std::fread(p, 1, s * n, static_cast<std::FILE*>(ud));
}

UltraNetResultCode MapCurlError(CURLcode rc, long status) {
    if (rc == CURLE_OK) {
        if (status >= 400) return UltraNetResultCode::HttpError;
        return UltraNetResultCode::Success;
    }
    switch (rc) {
        case CURLE_URL_MALFORMAT:           return UltraNetResultCode::InvalidUrl;
        case CURLE_COULDNT_RESOLVE_HOST:    return UltraNetResultCode::HostNotFound;
        case CURLE_COULDNT_CONNECT:         return UltraNetResultCode::ConnectionRefused;
        case CURLE_OPERATION_TIMEDOUT:      return UltraNetResultCode::Timeout;
        case CURLE_LOGIN_DENIED:            return UltraNetResultCode::AuthenticationFailed;
        case CURLE_SSL_CONNECT_ERROR:       return UltraNetResultCode::TlsHandshakeFailed;
        case CURLE_PEER_FAILED_VERIFICATION:return UltraNetResultCode::TlsCertificateInvalid;
        default:                            return UltraNetResultCode::Unknown;
    }
}

// Rewrite webdav:// → http:// and webdavs:// → https:// for libcurl.
std::string ToHttpUrl(const std::string& url) {
    if (url.rfind("webdavs://", 0) == 0) return "https://" + url.substr(10);
    if (url.rfind("webdav://",  0) == 0) return "http://"  + url.substr(9);
    return url;
}

void ApplyCommon(CURL* h, const UltraNetFileShareOptions& o) {
    if (!o.credentials.username.empty()) {
        curl_easy_setopt(h, CURLOPT_HTTPAUTH, CURLAUTH_BASIC | CURLAUTH_DIGEST);
        curl_easy_setopt(h, CURLOPT_USERNAME, o.credentials.username.c_str());
        curl_easy_setopt(h, CURLOPT_PASSWORD, o.credentials.password.c_str());
    }
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(o.connectTimeoutMs));
    curl_easy_setopt(h, CURLOPT_TIMEOUT_MS,        static_cast<long>(o.operationTimeoutMs));
    curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER,    o.verifyTls ? 1L : 0L);
    curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST,    o.verifyTls ? 2L : 0L);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
}

UltraNetResult Run(CURL* h, const std::string& url) {
    CURLcode rc = curl_easy_perform(h);
    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    UltraNetResult r;
    r.url = url; r.httpStatus = static_cast<int>(status);
    r.code = MapCurlError(rc, status);
    r.success = (r.code == UltraNetResultCode::Success);
    if (!r.success && rc != CURLE_OK) r.message = curl_easy_strerror(rc);
    else if (!r.success) r.message = "HTTP " + std::to_string(status);
    return r;
}

// ===== PROPFIND XML extractor ===============================================
// Minimal but correct for typical WebDAV server output (nginx, Apache mod_dav,
// nextcloud, sabredav). Splits the body on <response> elements, then pulls
// href / displayname / getcontentlength / getlastmodified / resourcetype out
// of each block via regex. Namespace-prefix-agnostic (matches D: / d: / no
// prefix). Not a full DAV: parser — good enough for v0.1 listing.

std::string ExtractTag(const std::string& body, const std::string& tag) {
    // Matches <[ns:]tag ...>VALUE</[ns:]tag>, where ns is any letters.
    const std::regex re(R"(<(?:[A-Za-z][A-Za-z0-9]*:)?)" + tag +
                        R"((?:\s[^>]*)?>([\s\S]*?)</(?:[A-Za-z][A-Za-z0-9]*:)?)" +
                        tag + R"(>)",
                        std::regex::icase);
    std::smatch m;
    if (std::regex_search(body, m, re)) return m[1].str();
    return {};
}

bool HasSelfClosingTag(const std::string& body, const std::string& tag) {
    const std::regex re(R"(<(?:[A-Za-z][A-Za-z0-9]*:)?)" + tag +
                        R"((?:\s[^>]*)?/>)",
                        std::regex::icase);
    return std::regex_search(body, re);
}

std::string Trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(0, 1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.pop_back();
    return s;
}

// Splits a PROPFIND multistatus response into one substring per <response>.
std::vector<std::string> SplitResponses(const std::string& body) {
    std::vector<std::string> out;
    const std::regex openTag(
        R"(<(?:[A-Za-z][A-Za-z0-9]*:)?response(?:\s[^>]*)?>)", std::regex::icase);
    const std::regex closeTag(
        R"(</(?:[A-Za-z][A-Za-z0-9]*:)?response>)", std::regex::icase);
    auto it = std::sregex_iterator(body.begin(), body.end(), openTag);
    const auto end = std::sregex_iterator();
    for (; it != end; ++it) {
        const std::size_t start = it->position(0) + it->length(0);
        std::smatch close;
        std::string tail = body.substr(start);
        if (std::regex_search(tail, close, closeTag)) {
            out.push_back(tail.substr(0, close.position(0)));
        }
    }
    return out;
}

void ParseMultistatus(const std::string& body,
                      const std::string& listUrl,
                      std::vector<UltraNetFtpEntry>& out) {
    for (const auto& resp : SplitResponses(body)) {
        UltraNetFtpEntry e;
        std::string href = Trim(ExtractTag(resp, "href"));
        if (href.empty()) continue;
        // The href is URL-encoded; decode the last path segment as the name.
        std::string decodedHref = UltraNet_UrlDecode(href);
        std::string name;
        std::size_t lastSlash = decodedHref.find_last_of('/');
        if (lastSlash != std::string::npos && lastSlash + 1 < decodedHref.size()) {
            name = decodedHref.substr(lastSlash + 1);
        } else if (lastSlash != std::string::npos) {
            // Trailing-slash href = directory; back up one segment.
            std::string trimmed = decodedHref.substr(0, lastSlash);
            std::size_t prev = trimmed.find_last_of('/');
            name = trimmed.substr(prev != std::string::npos ? prev + 1 : 0);
        }
        if (name.empty() || name == "." || name == "..") continue;

        const std::string display = Trim(ExtractTag(resp, "displayname"));
        if (!display.empty()) name = display;
        e.name     = name;
        e.fullPath = listUrl + name;

        // resourcetype: presence of <collection/> means directory
        std::string resourceType = ExtractTag(resp, "resourcetype");
        if (HasSelfClosingTag(resourceType, "collection") ||
            ExtractTag(resourceType, "collection").size() ||
            (decodedHref.size() > 1 && decodedHref.back() == '/')) {
            e.type = UltraNetFtpEntryType::Directory;
        } else {
            e.type = UltraNetFtpEntryType::File;
        }
        const std::string size = Trim(ExtractTag(resp, "getcontentlength"));
        if (!size.empty()) e.size = std::atoll(size.c_str());
        e.modificationTime = Trim(ExtractTag(resp, "getlastmodified"));

        out.push_back(std::move(e));
    }
}

// ===== Plug-in ===============================================================

class WebDavPlugin : public IFileShareProtocolPlugin {
public:
    std::string GetName() const override { return "UltraNet-WebDAV"; }
    std::string GetVersion() const override { return "0.1.0"; }
    std::vector<std::string> GetSupportedSchemes() const override {
        return {"webdav", "webdavs"};
    }
    UltraNetResult Initialize(const UltraNetConfig&) override {
        return UltraNetResult::Ok();
    }
    void Shutdown() override {}

    UltraNetResult ListDirectory(
        const std::string& url,
        std::vector<UltraNetFtpEntry>& out,
        const UltraNetFileShareOptions& opt) override {
        out.clear();
        std::string listUrl = ToHttpUrl(url);
        if (!listUrl.empty() && listUrl.back() != '/') listUrl.push_back('/');

        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> h(
            curl_easy_init(), curl_easy_cleanup);
        if (!h) return UltraNetResult::Error(
            UltraNetResultCode::InsufficientMemory, "curl_easy_init failed");

        // Minimal PROPFIND body — request the props we actually use.
        static const char* kBody =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<D:propfind xmlns:D=\"DAV:\">"
              "<D:prop>"
                "<D:displayname/>"
                "<D:getcontentlength/>"
                "<D:getlastmodified/>"
                "<D:getcontenttype/>"
                "<D:resourcetype/>"
              "</D:prop>"
            "</D:propfind>";

        curl_slist* hdrs = nullptr;
        hdrs = curl_slist_append(hdrs, "Depth: 1");
        hdrs = curl_slist_append(hdrs, "Content-Type: application/xml; charset=\"utf-8\"");
        std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>
            hdrsGuard(hdrs, curl_slist_free_all);

        std::string body;
        curl_easy_setopt(h.get(), CURLOPT_URL, listUrl.c_str());
        curl_easy_setopt(h.get(), CURLOPT_CUSTOMREQUEST, "PROPFIND");
        curl_easy_setopt(h.get(), CURLOPT_HTTPHEADER, hdrs);
        curl_easy_setopt(h.get(), CURLOPT_POSTFIELDS, kBody);
        curl_easy_setopt(h.get(), CURLOPT_POSTFIELDSIZE,
                         static_cast<long>(std::strlen(kBody)));
        curl_easy_setopt(h.get(), CURLOPT_WRITEFUNCTION, &WriteString);
        curl_easy_setopt(h.get(), CURLOPT_WRITEDATA, &body);
        ApplyCommon(h.get(), opt);

        UltraNetResult r = Run(h.get(), listUrl);
        // PROPFIND returns 207 Multi-Status on success; map that to ok.
        if (r.httpStatus == 207) {
            r.code = UltraNetResultCode::Success;
            r.success = true;
            r.message.clear();
        }
        if (r.success) ParseMultistatus(body, listUrl, out);
        return r;
    }

    UltraNetResult Download(const std::string& url,
                            const std::string& localPath,
                            const UltraNetFileShareOptions& opt) override {
        std::FILE* fp = std::fopen(localPath.c_str(), "wb");
        if (!fp) return UltraNetResult::Error(
            UltraNetResultCode::AccessDenied, "cannot open local file");
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> h(
            curl_easy_init(), curl_easy_cleanup);
        if (!h) { std::fclose(fp); return UltraNetResult::Error(
            UltraNetResultCode::InsufficientMemory, "curl_easy_init failed"); }
        const std::string u = ToHttpUrl(url);
        curl_easy_setopt(h.get(), CURLOPT_URL, u.c_str());
        curl_easy_setopt(h.get(), CURLOPT_WRITEFUNCTION, &WriteFile);
        curl_easy_setopt(h.get(), CURLOPT_WRITEDATA, fp);
        ApplyCommon(h.get(), opt);
        UltraNetResult r = Run(h.get(), u);
        std::fclose(fp);
        return r;
    }

    UltraNetResult Upload(const std::string& localPath,
                          const std::string& url,
                          const UltraNetFileShareOptions& opt) override {
        std::FILE* fp = std::fopen(localPath.c_str(), "rb");
        if (!fp) return UltraNetResult::Error(
            UltraNetResultCode::NotFound, "cannot open local file");
        std::fseek(fp, 0, SEEK_END);
        const long size = std::ftell(fp);
        std::fseek(fp, 0, SEEK_SET);
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> h(
            curl_easy_init(), curl_easy_cleanup);
        if (!h) { std::fclose(fp); return UltraNetResult::Error(
            UltraNetResultCode::InsufficientMemory, "curl_easy_init failed"); }
        const std::string u = ToHttpUrl(url);
        curl_easy_setopt(h.get(), CURLOPT_URL, u.c_str());
        curl_easy_setopt(h.get(), CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(h.get(), CURLOPT_READFUNCTION, &ReadFile);
        curl_easy_setopt(h.get(), CURLOPT_READDATA, fp);
        curl_easy_setopt(h.get(), CURLOPT_INFILESIZE_LARGE,
                         static_cast<curl_off_t>(size > 0 ? size : 0));
        ApplyCommon(h.get(), opt);
        UltraNetResult r = Run(h.get(), u);
        std::fclose(fp);
        return r;
    }

    UltraNetResult Delete(const std::string& url,
                          const UltraNetFileShareOptions& opt) override {
        return SimpleMethod(url, "DELETE", opt);
    }

    UltraNetResult CreateDirectory(const std::string& url,
                                   const UltraNetFileShareOptions& opt) override {
        std::string u = ToHttpUrl(url);
        if (!u.empty() && u.back() != '/') u.push_back('/');
        return SimpleMethod(u, "MKCOL", opt);
    }

    UltraNetResult Move(const std::string& source, const std::string& dest,
                        const UltraNetFileShareOptions& opt) override {
        return DestMethod(source, dest, "MOVE", opt);
    }

    UltraNetResult Copy(const std::string& source, const std::string& dest,
                        const UltraNetFileShareOptions& opt) override {
        return DestMethod(source, dest, "COPY", opt);
    }

private:
    UltraNetResult SimpleMethod(const std::string& url, const char* method,
                                const UltraNetFileShareOptions& opt) {
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> h(
            curl_easy_init(), curl_easy_cleanup);
        if (!h) return UltraNetResult::Error(
            UltraNetResultCode::InsufficientMemory, "curl_easy_init failed");
        const std::string u = ToHttpUrl(url);
        curl_easy_setopt(h.get(), CURLOPT_URL, u.c_str());
        curl_easy_setopt(h.get(), CURLOPT_CUSTOMREQUEST, method);
        curl_easy_setopt(h.get(), CURLOPT_NOBODY, 1L);
        ApplyCommon(h.get(), opt);
        return Run(h.get(), u);
    }

    UltraNetResult DestMethod(const std::string& source, const std::string& dest,
                              const char* method,
                              const UltraNetFileShareOptions& opt) {
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> h(
            curl_easy_init(), curl_easy_cleanup);
        if (!h) return UltraNetResult::Error(
            UltraNetResultCode::InsufficientMemory, "curl_easy_init failed");
        const std::string srcUrl = ToHttpUrl(source);
        const std::string dstUrl = ToHttpUrl(dest);
        const std::string destHdr = "Destination: " + dstUrl;

        curl_slist* hdrs = curl_slist_append(nullptr, destHdr.c_str());
        std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>
            hdrsGuard(hdrs, curl_slist_free_all);

        curl_easy_setopt(h.get(), CURLOPT_URL, srcUrl.c_str());
        curl_easy_setopt(h.get(), CURLOPT_CUSTOMREQUEST, method);
        curl_easy_setopt(h.get(), CURLOPT_HTTPHEADER, hdrs);
        curl_easy_setopt(h.get(), CURLOPT_NOBODY, 1L);
        ApplyCommon(h.get(), opt);
        return Run(h.get(), srcUrl);
    }
};

} // namespace

extern "C" ULTRANET_PLUGIN_EXPORT
void UltraNet_PluginInit(const UltraNetPluginHost* host) {
    if (!host || host->abiVersion < 1 || !host->RegisterPlugin) return;
    host->RegisterPlugin(std::make_shared<WebDavPlugin>());
}
#if !defined(_WIN32) && !defined(_WIN64)  // v1 resolves UltraNet_RegisterPlugin from the host at dlopen(); POSIX-only, Windows uses the v2 UltraNet_PluginInit vtable above
extern "C" void UltraNet_PluginRegister(void) {
    UltraNet_RegisterPlugin(std::make_shared<WebDavPlugin>());
}
#endif
