// Apps/UltraStore/web/WebPlatform.cpp
// The two sides of WebPlatform.h: the browser, and the desktop over a
// publish folder.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "WebPlatform.h"

#include "UltraCanvasSystemLanguage.h"
#include "UltraCanvasUtils.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace UltraStore::Web {

namespace {
std::map<std::string, std::string> gOptions;   // natively: the command line; in the browser: read from <html>
std::function<void()> gOnNavigate;
} // namespace

void Platform::Log(const std::string& line) {
    std::printf("[ultrastore-web] %s\n", line.c_str());
    std::fflush(stdout);
}

std::vector<std::string> Platform::Languages() {
    return UltraCanvas::SystemLanguageTags();
}

} // namespace UltraStore::Web

#if defined(__EMSCRIPTEN__)
// ===========================================================================
// The browser
// ===========================================================================
#include "OS/WASM/UltraCanvasWASMSupport.h"

#include <emscripten.h>
#include <emscripten/html5.h>

namespace UltraStore::Web {

namespace {
std::string gRoot;   // the site's root relative to the page ("", "../../")

// The formats this browser decodes, asked once: WebP synchronously (a
// canvas that can encode it decodes it), AVIF by decoding a one-pixel
// picture, which answers a moment later; until then AVIF is not chosen.
void ProbeFormats() {
    EM_ASM({
        Module.ucStoreFormats = { webp: 0, avif: 0 };
        try {
            var c = document.createElement('canvas');
            c.width = c.height = 1;
            Module.ucStoreFormats.webp = c.toDataURL('image/webp').indexOf('data:image/webp') === 0 ? 1 : 0;
        } catch (e) { /* no canvas: no webp */ }
        var img = new Image();
        img.onload = function() { Module.ucStoreFormats.avif = img.width === 1 ? 1 : 0; };
        img.onerror = function() { Module.ucStoreFormats.avif = 0; };
        img.src = 'data:image/avif;base64,AAAAIGZ0eXBhdmlmAAAAAGF2aWZtaWYxbWlhZk1BMUIAAADybWV0YQAAAAAAAAAoaGRscgAAAAAAAAAAcGljdAAAAAAAAAAAAAAAAGxpYmF2aWYAAAAADnBpdG0AAAAAAAEAAAAeaWxvYwAAAABEAAABAAEAAAABAAABGgAAAB0AAAAoaWluZgAAAAAAAQAAABppbmZlAgAAAAABAABhdjAxQ29sb3IAAAAAamlwcnAAAABLaXBjbwAAABRpc3BlAAAAAAAAAAIAAAACAAAAEHBpeGkAAAAAAwgICAAAAAxhdjFDgQ0MAAAAABNjb2xybmNseAACAAIAAYAAAAAXaXBtYQAAAAAAAAABAAEEAQKDBAAAACVtZGF0EgAKCBgANogQEAwgMg8f8D///8WfhwB8+ErK42A=';
    });
}

std::string ReadHtmlOption(const char* name) {
    char* text = static_cast<char*>(EM_ASM_PTR({
        var value = document.documentElement.dataset[UTF8ToString($0)] || '';
        return stringToNewUTF8(value);
    }, name));
    std::string out = text ? text : "";
    std::free(text);
    return out;
}

// The verification runs as a promise in the page; the store asks again
// every few milliseconds until it is done (no Asyncify, no thread).
struct PendingVerify {
    std::function<void(bool, const std::string&)> done;
};
void PollVerify(void* arg) {
    auto* pending = static_cast<PendingVerify*>(arg);
    const int state = EM_ASM_INT({
        var v = Module.ucStoreVerify;
        if (!v || !v.done) return 0;
        return v.ok ? 1 : 2;
    });
    if (state == 0) { emscripten_async_call(PollVerify, arg, 20); return; }
    std::string error;
    if (state == 2) {
        char* text = static_cast<char*>(EM_ASM_PTR({ return stringToNewUTF8(String((Module.ucStoreVerify && Module.ucStoreVerify.error) || '')); }));
        error = text ? text : "";
        std::free(text);
    }
    auto done = std::move(pending->done);
    delete pending;
    done(state == 1, error);
}
} // namespace

bool Platform::Open(const std::string&, std::string&) {
    gRoot = ReadHtmlOption("root");
    ProbeFormats();
    return true;
}

void Platform::FetchText(const std::string& relativeUrl, std::function<void(bool, const std::string&)> done) {
    UltraCanvas::WASMNetwork::FetchTextAsync(gRoot + relativeUrl, [done = std::move(done)](bool ok, const std::string& text) { done(ok, text); });
}

void Platform::LoadPicture(const std::string& relativeUrl, std::function<void(std::shared_ptr<UltraCanvas::UCImage>)> done) {
    UltraCanvas::WASMResourceLoader::LoadImage(gRoot + relativeUrl,
        [done = std::move(done)](bool ok, int width, int height, const std::vector<uint8_t>& pixels) {
            if (!ok || width <= 0 || height <= 0) { done(nullptr); return; }
            done(UltraCanvas::UCImageRaster::FromRgba(pixels.data(), width, height));
        });
}

bool Platform::DecodesFormat(const std::string& format) {
    if (format == "png" || format == "jpeg" || format == "jpg" || format == "gif") return true;
    if (format == "webp") return EM_ASM_INT({ return (Module.ucStoreFormats && Module.ucStoreFormats.webp) ? 1 : 0; }) == 1;
    if (format == "avif") return EM_ASM_INT({ return (Module.ucStoreFormats && Module.ucStoreFormats.avif) ? 1 : 0; }) == 1;
    return false;
}

void Platform::Verify(const std::string& catalogBytes, const Bytes& signature, const std::vector<Bytes>& keys,
                      std::function<void(bool, const std::string&)> done) {
    std::vector<uint8_t> flat;
    for (const Bytes& k : keys) if (k.size() == 32) flat.insert(flat.end(), k.begin(), k.end());
    if (flat.empty()) { done(false, "no trusted key"); return; }
    // WebCrypto's Ed25519 (Chromium, Firefox and Safari all have it): each
    // key tried until one verifies; a browser without it says so.
    EM_ASM({
        var data = HEAPU8.slice($0, $0 + $1);
        var sig = HEAPU8.slice($2, $2 + $3);
        var keyBytes = HEAPU8.slice($4, $4 + $5);
        Module.ucStoreVerify = { done: false, ok: false, error: '' };
        (async function() {
            try {
                if (!(window.crypto && window.crypto.subtle)) throw new Error('this browser has no WebCrypto (a secure page is needed)');
                for (var at = 0; at + 32 <= keyBytes.length; at += 32) {
                    var key = await crypto.subtle.importKey('raw', keyBytes.slice(at, at + 32), { name: 'Ed25519' }, false, ['verify']);
                    if (await crypto.subtle.verify({ name: 'Ed25519' }, key, sig, data)) { Module.ucStoreVerify.ok = true; break; }
                }
                if (!Module.ucStoreVerify.ok) Module.ucStoreVerify.error = 'no trusted key signed this catalog';
            } catch (e) {
                Module.ucStoreVerify.error = 'the signature could not be checked: ' + (e && e.message ? e.message : e);
            }
            Module.ucStoreVerify.done = true;
        })();
    }, catalogBytes.data(), static_cast<int>(catalogBytes.size()), signature.data(), static_cast<int>(signature.size()),
       flat.data(), static_cast<int>(flat.size()));
    emscripten_async_call(PollVerify, new PendingVerify{std::move(done)}, 20);
}

std::string Platform::ReadState() { return UltraCanvas::WASMBrowser::GetLocalStorage("ultrastore.web.state"); }
void Platform::WriteState(const std::string& json) { UltraCanvas::WASMBrowser::SetLocalStorage("ultrastore.web.state", json); }

double Platform::PixelRatio() { return emscripten_get_device_pixel_ratio(); }

std::string Platform::Page() {
    // The address first (?page=), else the page the HTML names (app/<id>/
    // index.html says which it is).
    const std::string fromAddress = UltraCanvas::WASMURL::GetQueryParameter("page");
    if (!fromAddress.empty() || UltraCanvas::WASMURL::HasQueryParameter("page")) return fromAddress;
    return ReadHtmlOption("page");
}

void Platform::SetPage(const std::string& page, bool addHistory) {
    // Relative to the site's root, so a page opened at app/<id>/ moves to
    // the root's address, where every page is ?page=<page>.
    const std::string url = gRoot.empty() && page.empty() ? std::string("?page=") : gRoot + (page.empty() ? std::string("") : "?page=" + page);
    if (addHistory) UltraCanvas::WASMURL::PushUrl(url.empty() ? "." : url);
    else UltraCanvas::WASMURL::ReplaceUrl(url.empty() ? "." : url);
}

void Platform::OnNavigate(std::function<void()> callback) {
    gOnNavigate = std::move(callback);
    UltraCanvas::WASMURL::SetOnNavigate([](const std::string&) { if (gOnNavigate) gOnNavigate(); });
}

void Platform::OpenExternal(const std::string& url) {
    // A custom scheme (ultrastore://) is handed to the system in the same
    // tab, so a browser without the store shows its own notice and the page
    // stays; an ordinary link opens in a new tab.
    EM_ASM({
        var url = UTF8ToString($0);
        if (url.indexOf('ultrastore://') === 0) window.location.href = url;
        else window.open(url, '_blank', 'noopener');
    }, url.c_str());
}

std::string Platform::Option(const std::string& name) {
    // data-store-download -> dataset.storeDownload
    std::string camel;
    bool upper = false;
    for (char c : name) {
        if (c == '-') { upper = true; continue; }
        camel += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
        upper = false;
    }
    return ReadHtmlOption(camel.c_str());
}

} // namespace UltraStore::Web

#else
// ===========================================================================
// The desktop: a publish folder
// ===========================================================================
#include "UltraCanvasDataFolder.h"
#include "UltraCanvasPathUtf8.h"
#include "Verifier.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace UltraStore::Web {

namespace {
namespace fs = std::filesystem;
fs::path gSite;
std::string gPage;

std::string ReadWhole(const fs::path& path, bool& ok) {
    std::ifstream in(path, std::ios::binary);
    ok = static_cast<bool>(in);
    if (!ok) return {};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

fs::path StateFile() {
    return UltraCanvas::UltraCanvasDataFolderRoot() / "UltraStoreWeb" / "state.json";
}
} // namespace

bool Platform::Open(const std::string& site, std::string& error) {
    std::error_code ec;
    gSite = UltraCanvas::PathFromUtf8(site);
    if (site.empty() || !fs::is_regular_file(gSite / "catalog.json", ec)) {
        error = "No catalog.json in " + (site.empty() ? std::string("(no folder given)") : site) +
                ": name a publish folder of the admin backend (ultrastoreadmin publish)";
        return false;
    }
    return true;
}

void Platform::FetchText(const std::string& relativeUrl, std::function<void(bool, const std::string&)> done) {
    bool ok = false;
    const std::string text = ReadWhole(gSite / UltraCanvas::PathFromUtf8(relativeUrl), ok);
    done(ok, text);
}

void Platform::LoadPicture(const std::string& relativeUrl, std::function<void(std::shared_ptr<UltraCanvas::UCImage>)> done) {
    const std::string path = UltraCanvas::PathToUtf8(gSite / UltraCanvas::PathFromUtf8(relativeUrl));
    auto image = UltraCanvas::UCImageRaster::Load(path, /*loadOnlyHeader=*/false);
    done(image && image->GetWidth() > 0 ? std::shared_ptr<UltraCanvas::UCImage>(image) : nullptr);
}

bool Platform::DecodesFormat(const std::string& format) {
    // The image library reads PNG, JPEG and WebP everywhere; AVIF needs a
    // decoder the desktop may lack, so the original is preferred to it.
    return format == "png" || format == "jpeg" || format == "jpg" || format == "webp";
}

void Platform::Verify(const std::string& catalogBytes, const Bytes& signature, const std::vector<Bytes>& keys,
                      std::function<void(bool, const std::string&)> done) {
    const Result<void> verified = VerifyCatalogSignature(catalogBytes, signature, keys);
    done(verified.Ok(), verified.Ok() ? std::string() : verified.error.message);
}

std::string Platform::ReadState() {
    bool ok = false;
    return ReadWhole(StateFile(), ok);
}

void Platform::WriteState(const std::string& json) {
    std::error_code ec;
    fs::create_directories(StateFile().parent_path(), ec);
    std::ofstream out(StateFile(), std::ios::binary | std::ios::trunc);
    out << json;
}

double Platform::PixelRatio() { return 1.0; }

std::string Platform::Page() { return gPage; }
void Platform::SetPage(const std::string& page, bool) { gPage = page; }
void Platform::OnNavigate(std::function<void()> callback) { gOnNavigate = std::move(callback); }

void Platform::OpenExternal(const std::string& url) {
    Log("open " + url);
    if (url.rfind("ultrastore://", 0) != 0) UltraCanvas::OpenURL(url);
}

std::string Platform::Option(const std::string& name) {
    const auto it = gOptions.find(name);
    return it == gOptions.end() ? std::string() : it->second;
}

// The native main sets these (WebStoreMain.cpp).
void SetNativeOption(const std::string& name, const std::string& value) { gOptions[name] = value; }
void SetNativePage(const std::string& page) { gPage = page; }

} // namespace UltraStore::Web
#endif
