// Apps/UltraWeb/host/UltraWebLoader.cpp
// Address bar → module bytes (UltraWebLoader.h).
// Version: 0.1.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWebLoader.h"
#include "UltraWebDemoApp.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraNet/UltraNetHttp.h"
#include "UltraNet/UltraNetUrl.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

using namespace UltraCanvas;

namespace UltraWeb {

namespace {

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

bool StartsWith(const std::string& s, const char* prefix) {
    return s.compare(0, std::strlen(prefix), prefix) == 0;
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

bool IsHttp(const std::string& address) {
    const std::string lower = Lower(address.substr(0, 8));
    return StartsWith(lower, "http://") || StartsWith(lower, "https://");
}

std::string NotAnApp(const std::string& contentType) {
    if (Lower(contentType).find("text/html") != std::string::npos)
        return "This is an HTML page. UltraWeb runs WebAssembly apps for now; "
               "HTML pages come with the reader (Phase 2 of the UltraWeb plan).";
    return "This is not a WebAssembly app" + (contentType.empty() ? std::string(".") : " (Content-Type: " + contentType + ").");
}

} // namespace

std::string UltraWebLoader::Normalise(const std::string& typed) {
    const std::string address = Trim(typed);
    if (address.empty()) return "about:demo";
    const std::string lower = Lower(address);
    if (StartsWith(lower, "about:") || StartsWith(lower, "file://") || IsHttp(address)) return address;
    std::error_code ec;
    if (std::filesystem::exists(PathFromUtf8(address), ec)) return address;
    // "example.org/app.wasm": a host name, as a browser would read it.
    const bool hostLike = address.find(' ') == std::string::npos && address.find('.') != std::string::npos
                          && address[0] != '/' && address[0] != '.';
    return hostLike ? "https://" + address : address;
}

bool UltraWebLoader::LooksLikeModule(const std::vector<uint8_t>& bytes) {
    if (bytes.size() >= 4 && bytes[0] == 0x00 && bytes[1] == 'a' && bytes[2] == 's' && bytes[3] == 'm') return true;
    // WebAssembly text: "(module", after blanks and ";;" line comments.
    size_t i = 0;
    while (i < bytes.size()) {
        if (std::isspace(bytes[i])) { ++i; continue; }
        if (bytes[i] == ';' && i + 1 < bytes.size() && bytes[i + 1] == ';') {
            while (i < bytes.size() && bytes[i] != '\n') ++i;
            continue;
        }
        break;
    }
    static const char kModule[] = "(module";
    const size_t n = sizeof(kModule) - 1;
    return bytes.size() - std::min(i, bytes.size()) >= n && std::memcmp(bytes.data() + i, kModule, n) == 0;
}

UltraWebLoader::UltraWebLoader(std::function<void(std::function<void()>)> post)
    : post_(std::move(post)), current_(std::make_shared<uint64_t>(0)) {}

UltraWebLoader::~UltraWebLoader() { Cancel(); }

void UltraWebLoader::Cancel() { ++*current_; }

void UltraWebLoader::Load(const std::string& typed, std::function<void(LoadedApp)> done) {
    const uint64_t navigation = ++*current_;
    const std::string address = Normalise(typed);
    auto current = current_;
    auto post = post_;
    // Every answer goes through the UI thread's queue and is dropped there
    // if the user has navigated since.
    auto answer = [current, navigation, post, done](LoadedApp app) {
        post([current, navigation, done, app = std::move(app)]() {
            if (*current == navigation) done(app);
        });
    };

    if (IsHttp(address)) {
        UltraNetHttpRequest request;
        request.url = address;
        request.options.maxReceiveSize = kMaxModuleBytes;
        request.options.timeoutMs = 30000;
        UltraNet_HttpRequestAsync(request, [answer, address](const UltraNetResponse& response) {
            LoadedApp app;
            app.address = response.finalUrl.empty() ? address : response.finalUrl;
            if (response.statusCode == 0) {
                // No HTTP answer at all; UltraNet puts the transport's reason here.
                app.error = "Could not load " + address
                            + (response.statusMessage.empty() ? std::string(" (no answer).") : ": " + response.statusMessage);
            } else if (!response.IsSuccess()) {
                app.error = "The server answered " + std::to_string(response.statusCode) + " "
                            + response.statusMessage + " for " + address + ".";
            } else if (!LooksLikeModule(response.body)) {
                app.error = NotAnApp(response.contentType);
            } else {
                app.ok = true;
                app.module = response.body;
            }
            answer(std::move(app));
        });
        return;
    }

    answer(LoadOffline(address));
}

LoadedApp UltraWebLoader::LoadOffline(const std::string& typed) {
    const std::string address = Normalise(typed);
    LoadedApp app;
    app.address = address;
    if (IsHttp(address)) {
        app.error = address + " needs the network; open it in the window.";
    } else if (Lower(address) == "about:demo") {
        const char* wat = DemoAppWat();
        app.ok = true;
        app.address = "about:demo";
        app.module.assign(wat, wat + std::strlen(wat));
    } else if (StartsWith(Lower(address), "about:")) {
        app.error = "There is no page " + address + ". Try about:demo.";
    } else {
        app = LoadLocal(address);
    }
    return app;
}

LoadedApp UltraWebLoader::LoadLocal(const std::string& address) {
    LoadedApp app;
    app.address = address;
    // file:///home/me/app.wasm → /home/me/app.wasm, percent-escapes undone.
    std::string path = address;
    if (StartsWith(Lower(path), "file://")) path = UltraNet_UrlDecode(path.substr(7));
    std::FILE* file = OpenFileUtf8(path, "rb");
    if (!file) {
        app.error = "Cannot open " + path + ".";
        return app;
    }
    std::vector<uint8_t> bytes;
    uint8_t chunk[65536];
    size_t got;
    while ((got = std::fread(chunk, 1, sizeof(chunk), file)) > 0) {
        bytes.insert(bytes.end(), chunk, chunk + got);
        if (int64_t(bytes.size()) > kMaxModuleBytes) break;
    }
    std::fclose(file);
    if (int64_t(bytes.size()) > kMaxModuleBytes) {
        app.error = path + " is larger than " + std::to_string(kMaxModuleBytes / (1024 * 1024)) + " MB.";
    } else if (!LooksLikeModule(bytes)) {
        app.error = NotAnApp("");
    } else {
        app.ok = true;
        app.module = std::move(bytes);
    }
    return app;
}

} // namespace UltraWeb
