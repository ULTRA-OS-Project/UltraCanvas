// OS/WASM/UltraCanvasWASMSupport.cpp
// Optional browser utilities for the WebAssembly backend - see the header.
//
// Every asynchronous browser call ends in one of the extern "C" trampolines
// below: JavaScript calls `_UltraCanvasWasm...()` with the request id it was
// given, and the trampoline finds the C++ callback in the matching registry.
// Buffers that JavaScript fills for C++ are allocated through
// UltraCanvasWasmAlloc() so the C++ side owns and frees them.
// Version: 2.0.0
// Last Modified: 2026-09-07
// Author: UltraCanvas Framework

#include "UltraCanvasWASMSupport.h"
#include "UltraCanvasDebug.h"

#include <emscripten.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>

namespace UltraCanvas {
namespace {

    // Pending callbacks by request id. All registries are touched from the
    // main thread only, but the mutex costs nothing and keeps a stray
    // worker-thread caller from corrupting them.
    template <typename Callback>
    class CallbackRegistry {
    public:
        int Add(Callback cb) {
            std::lock_guard<std::mutex> lock(mutex);
            const int id = ++lastId;
            pending[id] = std::move(cb);
            return id;
        }
        Callback Take(int id) {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = pending.find(id);
            if (it == pending.end()) return {};
            Callback cb = std::move(it->second);
            pending.erase(it);
            return cb;
        }
    private:
        std::mutex mutex;
        std::map<int, Callback> pending;
        int lastId = 0;
    };

    CallbackRegistry<WASMFileSystem::SyncCallback>& SyncCallbacks() {
        static CallbackRegistry<WASMFileSystem::SyncCallback> r; return r;
    }
    CallbackRegistry<WASMNetwork::FetchCallback>& FetchCallbacks() {
        static CallbackRegistry<WASMNetwork::FetchCallback> r; return r;
    }
    CallbackRegistry<WASMBrowser::PickFilesCallback>& PickCallbacks() {
        static CallbackRegistry<WASMBrowser::PickFilesCallback> r; return r;
    }
    CallbackRegistry<WASMResourceLoader::ImageLoadCallback>& ImageCallbacks() {
        static CallbackRegistry<WASMResourceLoader::ImageLoadCallback> r; return r;
    }

    // Take ownership of a stringToNewUTF8() result.
    std::string TakeJsString(char* s) {
        if (!s) return {};
        std::string result(s);
        free(s);
        return result;
    }

    std::vector<std::string> SplitLines(const std::string& text) {
        std::vector<std::string> lines;
        size_t start = 0;
        while (start <= text.size()) {
            size_t end = text.find('\n', start);
            if (end == std::string::npos) end = text.size();
            if (end > start) lines.push_back(text.substr(start, end - start));
            start = end + 1;
        }
        return lines;
    }

    std::string PercentDecode(const std::string& in) {
        std::string out;
        out.reserve(in.size());
        for (size_t i = 0; i < in.size(); i++) {
            if (in[i] == '%' && i + 2 < in.size()) {
                char hex[3] = { in[i + 1], in[i + 2], 0 };
                char* end = nullptr;
                long v = std::strtol(hex, &end, 16);
                if (end == hex + 2) {
                    out.push_back(static_cast<char>(v));
                    i += 2;
                    continue;
                }
            }
            out.push_back(in[i]);
        }
        return out;
    }

    // Copy a file body out of the browser (fetch) into the virtual FS. Shared
    // by LoadFont and the file picker. Returns "" on failure.
    bool WriteBytes(const std::string& path, const uint8_t* data, size_t size) {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        if (size > 0) out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
        out.close();
        return out.good();
    }

} // namespace
} // namespace UltraCanvas

// ===== JAVASCRIPT -> C++ TRAMPOLINES =====

extern "C" {

EMSCRIPTEN_KEEPALIVE void* UltraCanvasWasmAlloc(int size) {
    return malloc(size > 0 ? static_cast<size_t>(size) : 1);
}

EMSCRIPTEN_KEEPALIVE void UltraCanvasWasmSyncDone(int id, int ok) {
    if (auto cb = UltraCanvas::SyncCallbacks().Take(id)) cb(ok != 0);
}

EMSCRIPTEN_KEEPALIVE void UltraCanvasWasmFetchDone(int id, int ok, uint8_t* data, int size) {
    std::vector<uint8_t> bytes;
    if (ok && data && size > 0) bytes.assign(data, data + size);
    if (data) free(data);
    if (auto cb = UltraCanvas::FetchCallbacks().Take(id)) cb(ok != 0, bytes);
}

EMSCRIPTEN_KEEPALIVE void UltraCanvasWasmFilesPicked(int id, char* joinedPaths) {
    const std::string joined = UltraCanvas::TakeJsString(joinedPaths);
    if (auto cb = UltraCanvas::PickCallbacks().Take(id)) cb(UltraCanvas::SplitLines(joined));
}

EMSCRIPTEN_KEEPALIVE void UltraCanvasWasmImageLoaded(int id, int ok, int width, int height,
                                                     uint8_t* pixels) {
    std::vector<uint8_t> rgba;
    if (ok && pixels && width > 0 && height > 0) {
        rgba.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
    }
    if (pixels) free(pixels);
    if (auto cb = UltraCanvas::ImageCallbacks().Take(id)) {
        cb(ok != 0 && !rgba.empty(), width, height, rgba);
    }
}

} // extern "C"

namespace UltraCanvas {

// ===== FILE SYSTEM SUPPORT =====

bool WASMFileSystem::MountFileSystem(const std::string& mountPoint, SyncCallback onLoaded) {
    const int id = SyncCallbacks().Add(std::move(onLoaded));
    const int mounted = EM_ASM_INT({
        try {
            var path = UTF8ToString($0);
            if (!FS.analyzePath(path).exists) FS.mkdirTree(path);
            FS.mount(IDBFS, {}, path);
            FS.syncfs(true, function(err) {
                if (err) console.error('UltraCanvas WASM: IDBFS load failed:', err);
                _UltraCanvasWasmSyncDone($1, err ? 0 : 1);
            });
            return 1;
        } catch (e) {
            console.error('UltraCanvas WASM: IDBFS mount failed:', e);
            return 0;
        }
    }, mountPoint.c_str(), id);
    if (!mounted) {
        if (auto cb = SyncCallbacks().Take(id)) cb(false);
        return false;
    }
    return true;
}

static bool SyncFs(bool populate, WASMFileSystem::SyncCallback onDone) {
    const int id = SyncCallbacks().Add(std::move(onDone));
    const int started = EM_ASM_INT({
        try {
            FS.syncfs($0 != 0, function(err) {
                if (err) console.error('UltraCanvas WASM: IDBFS sync failed:', err);
                _UltraCanvasWasmSyncDone($1, err ? 0 : 1);
            });
            return 1;
        } catch (e) {
            console.error('UltraCanvas WASM: IDBFS sync failed:', e);
            return 0;
        }
    }, populate ? 1 : 0, id);
    if (!started) {
        if (auto cb = SyncCallbacks().Take(id)) cb(false);
        return false;
    }
    return true;
}

bool WASMFileSystem::SyncFromBrowser(SyncCallback onDone) { return SyncFs(true, std::move(onDone)); }
bool WASMFileSystem::SyncToBrowser(SyncCallback onDone)   { return SyncFs(false, std::move(onDone)); }

bool WASMFileSystem::FileExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

std::vector<uint8_t> WASMFileSystem::ReadFile(const std::string& path) {
    std::vector<uint8_t> data;
    std::ifstream in(path, std::ios::binary);
    if (!in) return data;
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size <= 0) return data;
    in.seekg(0, std::ios::beg);
    data.resize(static_cast<size_t>(size));
    in.read(reinterpret_cast<char*>(data.data()), size);
    if (!in) data.clear();
    return data;
}

bool WASMFileSystem::WriteFile(const std::string& path, const std::vector<uint8_t>& data) {
    return WriteBytes(path, data.data(), data.size());
}

bool WASMFileSystem::DeleteFile(const std::string& path) {
    std::error_code ec;
    return std::filesystem::remove(path, ec) && !ec;
}

bool WASMFileSystem::CreateDirectory(const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return !ec && std::filesystem::is_directory(path, ec);
}

std::vector<std::string> WASMFileSystem::ListDirectory(const std::string& path) {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
        names.push_back(entry.path().filename().string());
    }
    return names;
}

// ===== NETWORK SUPPORT =====

void WASMNetwork::FetchAsync(const std::string& url, FetchCallback callback) {
    const int id = FetchCallbacks().Add(std::move(callback));
    EM_ASM({
        var id = $1;
        fetch(UTF8ToString($0)).then(function(response) {
            if (!response.ok) throw new Error('HTTP ' + response.status);
            return response.arrayBuffer();
        }).then(function(buffer) {
            var n = buffer.byteLength;
            var ptr = _UltraCanvasWasmAlloc(n);
            if (!ptr) throw new Error('out of memory');
            new Uint8Array(HEAPU8.buffer, ptr, n).set(new Uint8Array(buffer));
            _UltraCanvasWasmFetchDone(id, 1, ptr, n);
        }).catch(function(e) {
            console.error('UltraCanvas WASM: fetch failed:', e);
            _UltraCanvasWasmFetchDone(id, 0, 0, 0);
        });
    }, url.c_str(), id);
}

void WASMNetwork::FetchTextAsync(const std::string& url, TextCallback callback) {
    FetchAsync(url, [cb = std::move(callback)](bool ok, const std::vector<uint8_t>& data) {
        if (cb) cb(ok, std::string(data.begin(), data.end()));
    });
}

// ===== BROWSER INTEGRATION =====

bool WASMBrowser::DownloadFile(const std::string& filename, const std::vector<uint8_t>& data,
                               const std::string& mimeType) {
    return DownloadFile(filename, data.data(), data.size(), mimeType);
}

bool WASMBrowser::DownloadFile(const std::string& filename, const void* data, size_t size,
                               const std::string& mimeType) {
    if (!data && size > 0) return false;
    return EM_ASM_INT({
        try {
            // Copy out of the (shared, under -pthread) wasm heap first.
            var bytes = new Uint8Array($3);
            if ($3 > 0) bytes.set(new Uint8Array(HEAPU8.buffer, $2, $3));
            var blob = new Blob([bytes], { type: UTF8ToString($1) });
            var url = URL.createObjectURL(blob);
            var a = document.createElement('a');
            a.href = url;
            a.download = UTF8ToString($0);
            a.style.display = 'none';
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            setTimeout(function() { URL.revokeObjectURL(url); }, 1000);
            return 1;
        } catch (e) {
            console.error('UltraCanvas WASM: download failed:', e);
            return 0;
        }
    }, filename.c_str(), mimeType.c_str(), reinterpret_cast<uintptr_t>(data),
       static_cast<int>(size)) != 0;
}

void WASMBrowser::PickFilesAsync(const std::string& accept, bool multiple,
                                 PickFilesCallback callback, const std::string& targetDir) {
    const int id = PickCallbacks().Add(std::move(callback));
    EM_ASM({
        var id = $3;
        var dir = UTF8ToString($2);
        var input = document.createElement('input');
        input.type = 'file';
        input.multiple = ($1 != 0);
        var accept = UTF8ToString($0);
        if (accept) input.accept = accept;
        input.style.display = 'none';
        document.body.appendChild(input);

        var done = false;
        var finish = function(paths) {
            if (done) return;
            done = true;
            if (input.parentNode) input.parentNode.removeChild(input);
            window.removeEventListener('focus', onFocus);
            _UltraCanvasWasmFilesPicked(id, stringToNewUTF8(paths.join('\n')));
        };
        // Older browsers fire no 'cancel' event; a dismissed picker returns
        // focus to the window with no files selected.
        var onFocus = function() {
            setTimeout(function() {
                if (!done && (!input.files || input.files.length === 0)) finish([]);
            }, 1500);
        };
        input.addEventListener('change', function() {
            var files = Array.prototype.slice.call(input.files || []);
            if (files.length === 0) { finish([]); return; }
            try { FS.mkdirTree(dir); } catch (e) {}
            Promise.all(files.map(function(file) {
                return file.arrayBuffer().then(function(buffer) {
                    // Keep the browser's file name, minus any path separators.
                    var name = file.name.split('/').pop().split(String.fromCharCode(92)).pop();
                    var path = dir + '/' + name;
                    FS.writeFile(path, new Uint8Array(buffer));
                    return path;
                });
            })).then(finish).catch(function(e) {
                console.error('UltraCanvas WASM: importing picked files failed:', e);
                finish([]);
            });
        });
        input.addEventListener('cancel', function() { finish([]); });
        window.addEventListener('focus', onFocus);
        input.click();
    }, accept.c_str(), multiple ? 1 : 0, targetDir.c_str(), id);
}

void WASMBrowser::Alert(const std::string& message) {
    EM_ASM({ window.alert(UTF8ToString($0)); }, message.c_str());
}

bool WASMBrowser::Confirm(const std::string& message) {
    return EM_ASM_INT({ return window.confirm(UTF8ToString($0)) ? 1 : 0; }, message.c_str()) != 0;
}

bool WASMBrowser::Prompt(const std::string& message, const std::string& defaultValue,
                         std::string& value) {
    char* result = static_cast<char*>(EM_ASM_PTR({
        var r = window.prompt(UTF8ToString($0), UTF8ToString($1));
        if (r === null) return 0;
        return stringToNewUTF8(r);
    }, message.c_str(), defaultValue.c_str()));
    if (!result) return false;
    value = TakeJsString(result);
    return true;
}

std::string WASMBrowser::Prompt(const std::string& message, const std::string& defaultValue) {
    std::string value;
    return Prompt(message, defaultValue, value) ? value : std::string();
}

void WASMBrowser::ConsoleLog(const std::string& message) {
    EM_ASM({ console.log(UTF8ToString($0)); }, message.c_str());
}

void WASMBrowser::ConsoleWarn(const std::string& message) {
    EM_ASM({ console.warn(UTF8ToString($0)); }, message.c_str());
}

void WASMBrowser::ConsoleError(const std::string& message) {
    EM_ASM({ console.error(UTF8ToString($0)); }, message.c_str());
}

std::string WASMBrowser::GetUserAgent() {
    return TakeJsString(static_cast<char*>(EM_ASM_PTR({
        return stringToNewUTF8(navigator.userAgent || '');
    })));
}

std::string WASMBrowser::GetPlatform() {
    return TakeJsString(static_cast<char*>(EM_ASM_PTR({
        return stringToNewUTF8(navigator.platform || '');
    })));
}

void WASMBrowser::GetScreenSize(int& width, int& height) {
    width = EM_ASM_INT({ return screen.width; });
    height = EM_ASM_INT({ return screen.height; });
}

void WASMBrowser::SetLocalStorage(const std::string& key, const std::string& value) {
    EM_ASM({
        try { localStorage.setItem(UTF8ToString($0), UTF8ToString($1)); }
        catch (e) { console.error('UltraCanvas WASM: localStorage write failed:', e); }
    }, key.c_str(), value.c_str());
}

std::string WASMBrowser::GetLocalStorage(const std::string& key) {
    return TakeJsString(static_cast<char*>(EM_ASM_PTR({
        try {
            var v = localStorage.getItem(UTF8ToString($0));
            return (v === null) ? 0 : stringToNewUTF8(v);
        } catch (e) { return 0; }
    }, key.c_str())));
}

bool WASMBrowser::HasLocalStorage(const std::string& key) {
    return EM_ASM_INT({
        try { return localStorage.getItem(UTF8ToString($0)) === null ? 0 : 1; }
        catch (e) { return 0; }
    }, key.c_str()) != 0;
}

void WASMBrowser::RemoveLocalStorage(const std::string& key) {
    EM_ASM({
        try { localStorage.removeItem(UTF8ToString($0)); } catch (e) {}
    }, key.c_str());
}

void WASMBrowser::ClearLocalStorage() {
    EM_ASM({ try { localStorage.clear(); } catch (e) {} });
}

// ===== RESOURCE LOADING =====

void WASMResourceLoader::LoadImage(const std::string& url, ImageLoadCallback callback) {
    const int id = ImageCallbacks().Add(std::move(callback));
    EM_ASM({
        var id = $1;
        var img = new Image();
        img.crossOrigin = 'anonymous';
        img.onload = function() {
            try {
                var w = img.naturalWidth;
                var h = img.naturalHeight;
                var canvas = document.createElement('canvas');
                canvas.width = w;
                canvas.height = h;
                var ctx = canvas.getContext('2d');
                ctx.drawImage(img, 0, 0);
                var data = ctx.getImageData(0, 0, w, h).data;   // straight RGBA
                var ptr = _UltraCanvasWasmAlloc(data.length);
                if (!ptr) throw new Error('out of memory');
                new Uint8Array(HEAPU8.buffer, ptr, data.length).set(data);
                _UltraCanvasWasmImageLoaded(id, 1, w, h, ptr);
            } catch (e) {
                console.error('UltraCanvas WASM: image decode failed:', e);
                _UltraCanvasWasmImageLoaded(id, 0, 0, 0, 0);
            }
        };
        img.onerror = function() { _UltraCanvasWasmImageLoaded(id, 0, 0, 0, 0); };
        img.src = UTF8ToString($0);
    }, url.c_str(), id);
}

void WASMResourceLoader::LoadFont(const std::string& fontFamily, const std::string& url,
                                  FontLoadCallback callback, const std::string& targetDir) {
    // File name from the URL's last path segment, falling back to the family.
    std::string name = url.substr(url.find_last_of('/') == std::string::npos ? 0 : url.find_last_of('/') + 1);
    if (auto q = name.find_first_of("?#"); q != std::string::npos) name.erase(q);
    if (name.empty()) name = fontFamily + ".ttf";
    const std::string path = targetDir + "/" + name;

    WASMNetwork::FetchAsync(url, [path, cb = std::move(callback)](bool ok, const std::vector<uint8_t>& data) {
        bool written = ok && !data.empty() && WriteBytes(path, data.data(), data.size());
        if (!written) {
            debugOutput << "UltraCanvas WASM: font download failed: " << path << std::endl;
        }
        if (cb) cb(written, written ? path : std::string());
    });
}

void WASMResourceLoader::PreloadAsset(const std::string& url) {
    EM_ASM({
        var link = document.createElement('link');
        link.rel = 'preload';
        link.href = UTF8ToString($0);
        link.as = 'fetch';
        link.crossOrigin = 'anonymous';
        document.head.appendChild(link);
    }, url.c_str());
}

// ===== TIME & PERFORMANCE =====

double WASMTime::GetTime() {
    return emscripten_get_now() / 1000.0;
}

void WASMTime::PerformanceMark(const std::string& name) {
    EM_ASM({
        if (window.performance && performance.mark) performance.mark(UTF8ToString($0));
    }, name.c_str());
}

double WASMTime::PerformanceMeasure(const std::string& name, const std::string& startMark,
                                    const std::string& endMark) {
    return EM_ASM_DOUBLE({
        try {
            if (!window.performance || !performance.measure) return 0;
            performance.measure(UTF8ToString($0), UTF8ToString($1), UTF8ToString($2));
            var entries = performance.getEntriesByName(UTF8ToString($0));
            return entries.length > 0 ? entries[entries.length - 1].duration : 0;
        } catch (e) {
            console.error('UltraCanvas WASM: performance.measure failed:', e);
            return 0;
        }
    }, name.c_str(), startMark.c_str(), endMark.c_str());
}

// ===== URL & QUERY PARAMETERS =====

std::string WASMURL::GetCurrentURL() {
    return TakeJsString(static_cast<char*>(EM_ASM_PTR({
        return stringToNewUTF8(window.location.href);
    })));
}

std::string WASMURL::GetQueryParameter(const std::string& name) {
    return TakeJsString(static_cast<char*>(EM_ASM_PTR({
        var v = new URLSearchParams(window.location.search).get(UTF8ToString($0));
        return (v === null) ? 0 : stringToNewUTF8(v);
    }, name.c_str())));
}

bool WASMURL::HasQueryParameter(const std::string& name) {
    return EM_ASM_INT({
        return new URLSearchParams(window.location.search).has(UTF8ToString($0)) ? 1 : 0;
    }, name.c_str()) != 0;
}

std::vector<std::pair<std::string, std::string>> WASMURL::GetAllQueryParameters() {
    // One "key\tvalue" line per parameter, both sides percent-encoded so
    // neither a tab nor a newline in a value can break the framing.
    const std::string joined = TakeJsString(static_cast<char*>(EM_ASM_PTR({
        var lines = [];
        new URLSearchParams(window.location.search).forEach(function(value, key) {
            lines.push(encodeURIComponent(key) + '\t' + encodeURIComponent(value));
        });
        return stringToNewUTF8(lines.join('\n'));
    })));
    std::vector<std::pair<std::string, std::string>> params;
    for (const std::string& line : SplitLines(joined)) {
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        params.emplace_back(PercentDecode(line.substr(0, tab)), PercentDecode(line.substr(tab + 1)));
    }
    return params;
}

std::string WASMURL::GetHash() {
    return TakeJsString(static_cast<char*>(EM_ASM_PTR({
        return stringToNewUTF8(window.location.hash || '');
    })));
}

void WASMURL::Navigate(const std::string& url) {
    EM_ASM({ window.location.href = UTF8ToString($0); }, url.c_str());
}

void WASMURL::Reload() {
    EM_ASM({ window.location.reload(); });
}

} // namespace UltraCanvas
