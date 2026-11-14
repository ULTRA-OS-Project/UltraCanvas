// Apps/UltraStore/web/main.cpp
// The web store's entry: in the browser the runtime calls main() once the
// side module is loaded and the window fills the viewport; on the desktop
//
//   ultrastore-web <publish folder> [page] [--download-url <url>] [--width <px>] [--height <px>]
//
// opens the same pages over a folder the admin backend published, so a
// page can be looked at before the wasm pipeline runs (`page` as in the
// address bar: app/<id>, category/<id>, categories).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "WebPlatform.h"
#include "WebStore.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"

#include "UltraStoreBuiltin.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

#if !defined(__EMSCRIPTEN__)
namespace UltraStore::Web {
void SetNativeOption(const std::string& name, const std::string& value);
void SetNativePage(const std::string& page);
}
#endif

int main(int argc, char* argv[]) {
    // Static, as every UltraCanvas application in a browser: Run() never returns.
    static UltraCanvasApplication app;
    if (!app.Initialize(argv[0])) return 1;

    int width = 1280, height = 860;
    std::string site, page;
#if !defined(__EMSCRIPTEN__)
    std::vector<std::string> args(argv + 1, argv + argc);
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--download-url" && i + 1 < args.size()) UltraStore::Web::SetNativeOption("store-download", args[++i]);
        else if (a == "--contact-url" && i + 1 < args.size()) UltraStore::Web::SetNativeOption("store-contact", args[++i]);
        else if (a == "--width" && i + 1 < args.size()) width = std::max(320, std::atoi(args[++i].c_str()));    // locale-ok: a size typed on the command line
        else if (a == "--height" && i + 1 < args.size()) height = std::max(240, std::atoi(args[++i].c_str()));  // locale-ok: as above
        else if (site.empty()) site = a;
        else page = a;
    }
    UltraStore::Web::SetNativePage(page);
#endif
    std::string error;
    if (!UltraStore::Web::Platform::Open(site, error)) {
        std::fprintf(stderr, "%s\nusage: ultrastore-web <publish folder> [page] [--download-url <url>]\n", error.c_str());
        return 2;
    }

    WindowConfig config;
    config.title = "UltraStore";
    config.width = width;
    config.height = height;
    config.backgroundColor = Color(255, 255, 255, 255);
    auto window = CreateWindow(config);
    if (!window) return 1;

    // The keys the catalog may be signed with: the ones built in (the
    // store's keys/, and the test keys a CI build adds), hex to bytes.
    std::vector<std::vector<uint8_t>> keys;
    for (const char* hex : std::initializer_list<const char*>{ULTRASTORE_BUILTIN_KEYS}) {
        std::vector<uint8_t> bytes;
        if (UltraStore::FromHex(hex, bytes) && bytes.size() == 32) keys.push_back(std::move(bytes));
    }

    static std::unique_ptr<UltraStore::Web::WebStore> store;
    store = std::make_unique<UltraStore::Web::WebStore>(app, window, std::move(keys));
    store->Start();
#if defined(__EMSCRIPTEN__)
    window->Maximize();   // fill the browser viewport
#endif
    window->Show();
    app.Run();   // never returns in the browser
    return 0;
}
