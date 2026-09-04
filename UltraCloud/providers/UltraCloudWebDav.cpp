// UltraCloud/providers/UltraCloudWebDav.cpp
// Version: 0.2.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework / ULTRA OS
#include <UltraCloud/UltraCloudWebDav.h>

#include <UltraNet/UltraNetUrl.h>   // UltraNet_UrlEncode / Decode

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace UltraCloud {

// ---- Pure helpers -----------------------------------------------------------

std::string NormalizePath(const std::string& path) {
    std::string p = path;
    while (!p.empty() && p.back() == '/') p.pop_back();
    if (p.empty()) return "/";
    if (p.front() != '/') p.insert(p.begin(), '/');
    return p;
}

std::string EncodePath(const std::string& path) {
    std::string out;
    std::string segment;
    auto flush = [&]() { out += UltraNet_UrlEncode(segment); segment.clear(); };
    for (char c : path) {
        if (c == '/') { flush(); out.push_back('/'); }
        else segment.push_back(c);
    }
    flush();
    return out;
}

std::string JoinUrl(const std::string& base, const std::string& path) {
    std::string b = base;
    while (!b.empty() && b.back() == '/') b.pop_back();
    std::string p = path;
    if (p.empty()) return b + "/";
    if (p.front() != '/') p.insert(p.begin(), '/');
    return b + p;
}

std::string PublicFolderLink(const std::string& publicBaseUrl, const std::string& path) {
    if (publicBaseUrl.empty()) return "";
    return JoinUrl(publicBaseUrl, EncodePath(NormalizePath(path)));
}

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Text between the first "<...name>" and the matching "</...name>" after
// `from` in `lowerXml`, tolerant of a namespace prefix ("d:", "D:", none).
// Returns false when the element is absent. Positions refer to `lowerXml`.
bool FindElement(const std::string& lowerXml, const std::string& name, std::size_t from,
                 std::size_t limit, std::size_t& contentStart, std::size_t& contentEnd) {
    // An opening tag ends the local name with '>' or a space (attributes).
    std::size_t pos = from;
    while (pos < limit) {
        std::size_t lt = lowerXml.find('<', pos);
        if (lt == std::string::npos || lt >= limit) return false;
        std::size_t gt = lowerXml.find('>', lt);
        if (gt == std::string::npos || gt >= limit) return false;
        std::string tag = lowerXml.substr(lt + 1, gt - lt - 1);
        if (!tag.empty() && tag[0] != '/' && tag[0] != '?' && tag[0] != '!') {
            std::string local = tag.substr(0, tag.find_first_of(" \t\r\n/"));
            if (auto colon = local.find(':'); colon != std::string::npos) local = local.substr(colon + 1);
            if (local == name) {
                if (tag.back() == '/') { contentStart = contentEnd = gt + 1; return true; }
                // Find the matching close tag "</prefix:name>" or "</name>".
                std::size_t search = gt + 1;
                while (search < limit) {
                    std::size_t clt = lowerXml.find("</", search);
                    if (clt == std::string::npos || clt >= limit) return false;
                    std::size_t cgt = lowerXml.find('>', clt);
                    if (cgt == std::string::npos) return false;
                    std::string ctag = lowerXml.substr(clt + 2, cgt - clt - 2);
                    if (auto colon = ctag.find(':'); colon != std::string::npos) ctag = ctag.substr(colon + 1);
                    if (ctag == name) { contentStart = gt + 1; contentEnd = clt; return true; }
                    search = cgt + 1;
                }
                return false;
            }
        }
        pos = gt + 1;
    }
    return false;
}

std::string Trim(const std::string& s) {
    std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string DecodeEntities(std::string s) {
    auto rep = [&s](const char* from, const char* to) {
        for (std::size_t p; (p = s.find(from)) != std::string::npos;) s.replace(p, std::strlen(from), to);
    };
    rep("&amp;", "&"); rep("&lt;", "<"); rep("&gt;", ">"); rep("&quot;", "\""); rep("&apos;", "'");
    return s;
}

} // namespace

std::vector<Entry> ParseMultistatus(const std::string& xml, const std::string& folderPath) {
    std::vector<Entry> out;
    const std::string lower = Lower(xml);
    const std::string folder = NormalizePath(folderPath);

    std::size_t pos = 0;
    bool first = true;
    while (true) {
        std::size_t rs, re;
        if (!FindElement(lower, "response", pos, lower.size(), rs, re)) break;
        pos = re;

        std::size_t hs, he;
        if (!FindElement(lower, "href", rs, re, hs, he)) continue;
        std::string href = DecodeEntities(Trim(xml.substr(hs, he - hs)));
        // Strip scheme/host: keep the path part, decoded.
        if (auto p = href.find("://"); p != std::string::npos) {
            auto slash = href.find('/', p + 3);
            href = slash == std::string::npos ? "/" : href.substr(slash);
        }
        std::string decoded = UltraNet_UrlDecode(href);
        while (decoded.size() > 1 && decoded.back() == '/') decoded.pop_back();

        Entry e;
        e.name = decoded.substr(decoded.find_last_of('/') + 1);
        // The DAV root may carry a prefix (e.g. /remote.php/dav/files/user);
        // the entry's provider path is folder + name.
        e.path = folder == "/" ? "/" + e.name : folder + "/" + e.name;

        // Skip the listed folder itself: servers report the requested
        // collection as the first response (RFC 4918 convention), so the
        // first collection row is the container — for the root whatever its
        // href (the DAV root may carry a prefix), otherwise when its name
        // matches the folder's last segment.
        std::size_t ts, te;
        e.isDirectory = FindElement(lower, "collection", rs, re, ts, te);
        if (first) {
            first = false;
            if (e.isDirectory) {
                const std::string folderName =
                    folder == "/" ? "" : folder.substr(folder.find_last_of('/') + 1);
                if (folder == "/" || e.name == folderName) continue;
            }
        }
        if (e.name.empty()) continue;

        std::size_t ls, le;
        if (FindElement(lower, "getcontentlength", rs, re, ls, le))
            e.size = std::strtoll(Trim(xml.substr(ls, le - ls)).c_str(), nullptr, 10);
        std::size_t ms, me;
        if (FindElement(lower, "getlastmodified", rs, re, ms, me))
            e.modified = Trim(xml.substr(ms, me - ms));
        out.push_back(std::move(e));
    }

    // Folders first, then by name.
    std::stable_sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        if (a.isDirectory != b.isDirectory) return a.isDirectory;
        return Lower(a.name) < Lower(b.name);
    });
    return out;
}

// ---- WebDavProvider ---------------------------------------------------------

ProviderCapabilities WebDavProvider::Capabilities() const {
    ProviderCapabilities c;
    c.browse = true; c.upload = true;
    c.shareLinks = true;             // only with a public base URL; see CreateShareLink
    c.needsServerUrl = true;
    return c;
}

std::string WebDavProvider::DavUrl(const Account& account, const std::string& path) const {
    return JoinUrl(account.serverUrl, EncodePath(NormalizePath(path)));
}

Result WebDavProvider::Verify(const Account& account, const Credentials& credentials) {
    if (account.serverUrl.empty())
        return Result::Error(ResultCode::InvalidArgument, "the account needs a server URL");
    std::vector<Entry> ignored;
    return List(account, credentials, "/", ignored);
}

Result WebDavProvider::List(const Account& account, const Credentials& credentials,
                            const std::string& path, std::vector<Entry>& out) {
    out.clear();
    UltraNetHttpRequest req;
    req.url = DavUrl(account, path);
    if (req.url.back() != '/') req.url.push_back('/');
    req.method = UltraNetHttpMethod::Custom;
    req.customMethod = "PROPFIND";
    req.headers.Set("Depth", "1");
    req.headers.Set("Content-Type", "application/xml; charset=utf-8");
    const std::string body =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<d:propfind xmlns:d=\"DAV:\"><d:prop>"
        "<d:resourcetype/><d:getcontentlength/><d:getlastmodified/>"
        "</d:prop></d:propfind>";
    req.body.assign(body.begin(), body.end());

    UltraNetResponse resp;
    UltraNetResult net = Send(credentials, req, resp);
    Result r = FromHttp(net, resp, "list " + path);
    if (!r) return r;
    out = ParseMultistatus(std::string(resp.body.begin(), resp.body.end()), path);
    return Result::Ok();
}

Result WebDavProvider::MakeDirectory(const Account& account, const Credentials& credentials,
                                     const std::string& path) {
    UltraNetHttpRequest req;
    req.url = DavUrl(account, path);
    req.method = UltraNetHttpMethod::Custom;
    req.customMethod = "MKCOL";
    UltraNetResponse resp;
    UltraNetResult net = Send(credentials, req, resp);
    return FromHttp(net, resp, "create folder " + path);
}

Result WebDavProvider::Upload(const Account& account, const Credentials& credentials,
                              const std::string& localPath, const std::string& remotePath) {
    std::ifstream is(localPath, std::ios::binary);
    if (!is) return Result::Error(ResultCode::IoError, "cannot read " + localPath);
    UltraNetHttpRequest req;
    req.url = DavUrl(account, remotePath);
    req.method = UltraNetHttpMethod::Put;
    req.headers.Set("Content-Type", "application/octet-stream");
    req.body.assign(std::istreambuf_iterator<char>(is), std::istreambuf_iterator<char>());
    UltraNetResponse resp;
    UltraNetResult net = Send(credentials, req, resp);
    return FromHttp(net, resp, "upload " + remotePath);
}

Result WebDavProvider::Download(const Account& account, const Credentials& credentials,
                                const std::string& remotePath, const std::string& localPath) {
    UltraNetHttpRequest req;
    req.url = DavUrl(account, remotePath);
    req.method = UltraNetHttpMethod::Get;
    UltraNetResponse resp;
    UltraNetResult net = Send(credentials, req, resp);
    Result r = FromHttp(net, resp, "download " + remotePath);
    if (!r) return r;
    std::ofstream os(localPath, std::ios::binary | std::ios::trunc);
    if (!os) return Result::Error(ResultCode::IoError, "cannot write " + localPath);
    os.write(reinterpret_cast<const char*>(resp.body.data()),
             static_cast<std::streamsize>(resp.body.size()));
    return os ? Result::Ok() : Result::Error(ResultCode::IoError, "cannot write " + localPath);
}

Result WebDavProvider::CreateShareLink(const Account& account, const Credentials&,
                                       const std::string& remotePath,
                                       const ShareLinkOptions&, ShareLink& out) {
    if (account.publicBaseUrl.empty())
        return Result::Error(ResultCode::Unsupported,
                             "this WebDAV account has no public URL, so it cannot make links");
    out = ShareLink{};
    out.url = PublicFolderLink(account.publicBaseUrl, remotePath);
    return Result::Ok();
}

} // namespace UltraCloud
