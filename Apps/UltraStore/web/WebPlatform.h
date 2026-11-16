// Apps/UltraStore/web/WebPlatform.h
// What the web store needs from where it runs, behind one face: in a
// browser (the Emscripten build, a side module on the UltraCanvas runtime)
// the catalog comes from the site's origin, pictures are decoded by the
// browser, the signature is checked by the browser's own crypto, the trust
// state lives in localStorage and the address bar names the page; on the
// desktop (the native build, to look at the pages before the wasm pipeline
// runs) the same site is a publish folder on disk, the image library
// decodes, UltraCrypt checks, and a file under the data folder remembers.
// Everything is asynchronous the way the browser is; the native side
// answers at once, in the callback.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasImage.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraStore::Web {

using Bytes = std::vector<uint8_t>;

struct Platform {
    // Natively `site` is the publish folder (catalog.json in it); in the
    // browser it is ignored, the site is the page's origin. False with a
    // message when the folder is not there.
    static bool Open(const std::string& site, std::string& error);

    // A file of the site, relative to its root ("catalog.json",
    // "media/<id>/icon-64.webp"), as text.
    static void FetchText(const std::string& relativeUrl, std::function<void(bool ok, const std::string& text)> done);

    // A picture of the site, decoded: nullptr when it cannot be.
    static void LoadPicture(const std::string& relativeUrl, std::function<void(std::shared_ptr<UltraCanvas::UCImage>)> done);

    // Whether a picture in the format ("avif", "webp", "png", "jpeg") can be
    // shown here: the web store picks a variant by this (Catalog.h,
    // MediaVariant). The browser is asked once, at Open.
    static bool DecodesFormat(const std::string& format);

    // The catalog's Ed25519 signature against the keys (32 bytes each):
    // true when one of them signed it.
    static void Verify(const std::string& catalogBytes, const Bytes& signature, const std::vector<Bytes>& keys,
                       std::function<void(bool ok, const std::string& error)> done);

    // What the store remembers between visits (the trust state, JSON).
    static std::string ReadState();
    static void WriteState(const std::string& json);

    // The device pixel ratio: 2 on a high-density screen.
    static double PixelRatio();

    // The person's languages, best first ("de-AT", "de", "en").
    static std::vector<std::string> Languages();

    // The page the address names: "" (Discover), "categories",
    // "category/<id>", "app/<id>". Natively, the one given at start.
    static std::string Page();
    // Puts a page in the address bar: with a history entry the back button
    // returns to, or in place of the current one.
    static void SetPage(const std::string& page, bool addHistory);
    // The back and forward buttons: the callback reads Page() again.
    static void OnNavigate(std::function<void()> callback);

    // Opens a link outside the store: the native store's ultrastore://
    // link, the publisher's page, the store's download page.
    static void OpenExternal(const std::string& url);

    // What the page carries for the store (data-<name> on <html>, written
    // by the admin backend's publish): "store-download" (where the native
    // store is downloaded), "root" (the site's root relative to the page,
    // "../../" from app/<id>/). Natively from the command line.
    static std::string Option(const std::string& name);

    // A line on the console (the browser's; stdout natively): what the
    // browser test follows.
    static void Log(const std::string& line);
};

} // namespace UltraStore::Web
