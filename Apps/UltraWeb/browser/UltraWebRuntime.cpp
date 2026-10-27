// Apps/UltraWeb/browser/UltraWebRuntime.cpp
// The UltraWeb runtime for ordinary browsers (Chrome, Firefox): UltraCanvas
// compiled with Emscripten, running an UltraWeb app - the same app.wasm the
// UltraWeb browser runs - on the browser's own WebAssembly engine. The app
// stays small; the framework comes once, either with the UltraCanvas
// browser extension or from the site (Docs/UltraWeb/UltraWebInTheBrowser.md).
//
// The page's loader (ultraweb-loader.js) starts this program, waits for
// Module.onUltraWebReady, fetches the app and hands its bytes to
// uw_runtime_start. From there it is UltraWeb's own code: UltraWebGuest
// builds the app's elements under the window's app area, through the
// element ABI, with the same rules as on the desktop. Only the services are
// the browser's: its fetch, IndexedDB for storage, its clipboard.
//
// Version: 0.1.0 - first version
// Last Modified: 2026-10-27
// Author: UltraCanvas Framework / ULTRA OS

#include "../host/UltraWebFetch.h"
#include "../host/UltraWebGuest.h"
#include "../host/UltraWebStorage.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasWindow.h"
#include "WasmHost/UltraCanvasWasmHost.h"
#include "OS/WASM/UltraCanvasWASMSupport.h"

#include <emscripten.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>

using namespace UltraCanvas;
using namespace UltraWeb;

// The browser's fetch for one request. Without credentials, as the rules
// say; the browser itself applies CORS and mixed content, so a response that
// reaches script was allowed. Calls _uw_runtime_fetch_done once, unless
// uw_js_fetch_cancel ran first.
EM_JS(void, uw_js_fetch, (int id, const char* url, const char* method, const uint8_t* body, int bodyLength,
                          const char* contentType, int maxBytes, int timeoutMs), {
    const fetches = Module.ultraWebFetches || (Module.ultraWebFetches = new Map());
    const controller = new AbortController();
    fetches.set(id, controller);
    const timer = setTimeout(() => controller.abort(), timeoutMs);
    const init = { method: UTF8ToString(method), credentials: "omit", redirect: "follow", signal: controller.signal };
    const type = UTF8ToString(contentType);
    if (bodyLength > 0 || type) {
        init.body = HEAPU8.slice(body, body + bodyLength);
        init.headers = { "Content-Type": type || "text/plain;charset=UTF-8" };
    }
    const finish = (status, finalUrl, headers, bytes, error, tooLarge) => {
        clearTimeout(timer);
        if (!fetches.has(id)) return;
        fetches.delete(id);
        const data = _malloc(Math.max(1, bytes.length));
        HEAPU8.set(bytes, data);
        const urlText = stringToNewUTF8(finalUrl);
        const headerText = stringToNewUTF8(headers);
        const errorText = stringToNewUTF8(error);
        _uw_runtime_fetch_done(id, status, urlText, headerText, data, bytes.length, errorText, tooLarge ? 1 : 0);
    };
    fetch(UTF8ToString(url), init).then(async (response) => {
        const buffer = new Uint8Array(await response.arrayBuffer());
        const tooLarge = buffer.length > maxBytes;
        let headers = "";
        response.headers.forEach((value, name) => { headers += name + ": " + value + "\n"; });
        // A cross-origin answer reaches script only when the server allowed
        // this origin, and the browser keeps that header from script: say it
        // here, so the shared rules (FetchRules::Admit) see what the browser saw.
        if (response.type === "cors") headers += "access-control-allow-origin: " + location.origin + "\n";
        finish(response.status, response.url, headers, tooLarge ? new Uint8Array(0) : buffer, "", tooLarge);
    }).catch((e) => {
        finish(0, "", "", new Uint8Array(0), String(e && e.message ? e.message : e), false);
    });
});

EM_JS(void, uw_js_fetch_cancel, (int id), {
    const fetches = Module.ultraWebFetches;
    const controller = fetches && fetches.get(id);
    if (!controller) return;
    fetches.delete(id);
    controller.abort();
});

namespace {

std::shared_ptr<UltraCanvasWindow> gWindow;
std::shared_ptr<UltraCanvasContainer> gArea;
std::unique_ptr<UltraWebGuest> gGuest;
std::map<int, std::function<void(FetchResponse)>> gFetches;
int gNextFetch = 1;

void Post(std::function<void()> task) {
    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->PostToUIThread(std::move(task));
}

void Log(const std::string& line) { std::printf("[UltraWeb app] %s\n", line.c_str()); }

void ShowError(const std::string& title, const std::string& detail) {
    std::printf("[UltraWeb] %s: %s\n", title.c_str(), detail.c_str());
    gArea->ClearChildren();
    auto heading = CreateLabel("uwErrorTitle", title);
    heading->SetFontSize(16);
    heading->SetTextColor(Color(176, 32, 32));
    auto text = CreateLabel("uwErrorDetail", detail);
    text->SetWrap(TextWrap::WrapWord);
    gArea->AddChild(heading);
    gArea->AddChild(text);
    gArea->InvalidateLayout();
    gArea->RequestRedraw();
}

FetchService BrowserFetch() {
    return [](const FetchRequest& request, std::function<void(FetchResponse)> done) -> std::function<void()> {
        const int id = gNextFetch++;
        gFetches[id] = std::move(done);
        uw_js_fetch(id, request.url.c_str(), request.method.c_str(), request.body.data(), int(request.body.size()),
                    request.contentType.c_str(), int(FetchRules::kMaxResponseBytes), FetchRules::kTimeoutMs);
        return [id]() {
            gFetches.erase(id);
            uw_js_fetch_cancel(id);
        };
    };
}

// What the app reaches beside its elements, the browser's way.
GuestServices BrowserServices(const std::string& address) {
    GuestServices services;
    services.startTimer = [](uint32_t ms, bool repeat, std::function<void()> fire) -> uint32_t {
        auto* app = UltraCanvasApplicationBase::GetCurrent();
        return app ? app->StartTimer(ms, repeat, [fire](TimerId) { fire(); }) : 0;
    };
    services.stopTimer = [](uint32_t id) {
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->StopTimer(id);
    };
    services.fetch = BrowserFetch();
    std::string problem;
    services.storage = UltraWebStorage::Open(UltraWebStorage::DefaultDirectory(),
                                             UltraWebStorage::PartitionFor(address), problem);
    if (!problem.empty()) Log("UltraWeb: storage kept in memory only: " + problem);
    services.writeClipboard = [](const std::string& text) { return SetClipboardText(text); };
    return services;
}

void StartGuest(std::vector<uint8_t> module, const std::string& address) {
    gGuest.reset();
    gArea->ClearChildren();
    GuestOptions options;
    options.onLog = Log;
    options.address = address;
    options.services = BrowserServices(address);
    options.onFailure = [](const std::string& message) {
        gGuest.reset();
        ShowError("The app stopped", message);
    };
    options.defer = Post;
    std::string error;
    gGuest = UltraWebGuest::Start(module, gArea, std::move(options), error);
    if (!gGuest) {
        ShowError("The app did not start", error);
        return;
    }
    std::printf("[UltraWeb] running %s: %zu elements, %s\n", address.c_str(), gGuest->ElementCount(),
                UltraCanvasWasm_EngineDescription().c_str());
    EM_ASM({ if (Module.onUltraWebStarted) Module.onUltraWebStarted(); });
}

} // namespace

extern "C" {

// The page hands over an app: its bytes and the address it came from (its
// origin for fetch and storage). The bytes are the caller's; this copies
// them. Storage lives in IndexedDB, so the app starts once that is loaded.
EMSCRIPTEN_KEEPALIVE void uw_runtime_start(const uint8_t* bytes, int length, const char* address) {
    std::vector<uint8_t> module(bytes, bytes + length);
    std::string where = address ? address : "";
    static bool mounted = false;
    const std::string storage = UltraWebStorage::DefaultDirectory();
    if (mounted || storage.empty()) {
        StartGuest(std::move(module), where);
        return;
    }
    mounted = true;
    const bool started = WASMFileSystem::MountFileSystem(storage, [module, where](bool) mutable {
        Post([module = std::move(module), where]() mutable { StartGuest(std::move(module), where); });
    });
    if (!started) StartGuest(std::move(module), where);
}

// A fetch uw_js_fetch started has its answer. Every pointer is the
// runtime's own (malloc / stringToNewUTF8), freed here.
EMSCRIPTEN_KEEPALIVE void uw_runtime_fetch_done(int id, int status, char* finalUrl, char* headers, uint8_t* body,
                                                int bodyLength, char* error, int tooLarge) {
    FetchResponse response;
    response.status = status;
    response.finalUrl = finalUrl ? finalUrl : "";
    response.error = error ? error : "";
    if (status == 0 && response.error.empty()) response.error = "no answer";
    response.tooLarge = tooLarge != 0;
    response.body.assign(body, body + bodyLength);
    std::string lines = headers ? headers : "";
    for (size_t at = 0; at < lines.size();) {
        size_t end = lines.find('\n', at);
        if (end == std::string::npos) end = lines.size();
        const std::string line = lines.substr(at, end - at);
        const size_t colon = line.find(": ");
        if (colon != std::string::npos) response.headers.emplace_back(line.substr(0, colon), line.substr(colon + 2));
        at = end + 1;
    }
    free(finalUrl);
    free(headers);
    free(body);
    free(error);
    auto at = gFetches.find(id);
    if (at == gFetches.end()) return;   // cancelled
    auto done = std::move(at->second);
    gFetches.erase(at);
    done(std::move(response));
}

// For tests (Apps/UltraWeb/browser/test/runtime-test.mjs): the number of
// elements the running app holds, or -1 when none runs ...
EMSCRIPTEN_KEEPALIVE int uw_runtime_element_count() { return gGuest ? int(gGuest->ElementCount()) : -1; }

// ... and where the app's element showing `text` is, in page pixels:
// x, y, width, height into `out`; 0 when no element shows it.
EMSCRIPTEN_KEEPALIVE int uw_runtime_locate(const char* text, float* out) {
    if (!gGuest || !text) return 0;
    const std::string wanted = text;
    for (uint32_t handle = 2; handle < 4096; ++handle) {
        auto element = gGuest->ElementForTest(handle);
        if (!element) continue;
        std::string shown;
        if (auto label = std::dynamic_pointer_cast<UltraCanvasLabel>(element)) shown = label->GetText();
        else if (auto button = std::dynamic_pointer_cast<UltraCanvasButton>(element)) shown = button->GetText();
        else if (auto box = std::dynamic_pointer_cast<UltraCanvasCheckbox>(element)) shown = box->GetText();
        else if (auto input = std::dynamic_pointer_cast<UltraCanvasTextInput>(element)) shown = input->GetText();
        if (shown != wanted) continue;
        const Rect2Df box = element->GetBoundsInWindow();
        out[0] = box.x;
        out[1] = box.y;
        out[2] = box.width;
        out[3] = box.height;
        return 1;
    }
    return 0;
}

} // extern "C"

int main(int, char* argv[]) {
    // Static, not a stack local: Run() never returns in the browser (see
    // Docs/UltraCanvas/UltraCanvasWebAssembly.md).
    static UltraCanvasApplication app;
    if (!app.Initialize(argv[0])) return 1;

    WindowConfig config;
    config.title = "UltraWeb";
    config.width = 960;
    config.height = 640;
    gWindow = CreateWindow(config);
    if (!gWindow) return 1;

    // UC_ROOT_HANDLE: a flex column the app fills, as in an UltraWeb tab.
    gArea = CreateContainer("uwApp", 0, 0, 0, 0);
    gArea->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Pct(100));
    gArea->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Start);
    ContainerStyle plain;
    plain.autoShowScrollbars = false;
    gArea->SetContainerStyle(plain);
    gWindow->AddChild(gArea);
    gWindow->Maximize();   // the page's viewport
    gWindow->Show();

    // Periodic, not per write: IndexedDB is asynchronous, and a store
    // flushes on a later turn anyway (UltraWebGuest::StorageChanged).
    app.StartTimer(2000, true, [](TimerId) { WASMFileSystem::SyncToBrowser(); });

    EM_ASM({ if (Module.onUltraWebReady) Module.onUltraWebReady(); });
    app.Run();   // never returns in the browser
    return 0;
}
