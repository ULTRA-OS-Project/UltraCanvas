// OS/WASM/UltraCanvasWASMSupport.h
// Optional browser utilities for applications built on the WebAssembly
// backend: persistent storage, fetch, file import/export, browser dialogs,
// localStorage, timing and the page URL.
//
// Nothing in the framework depends on these; they exist so an application
// can reach the browser features that have no desktop equivalent without
// writing EM_ASM itself. Everything that touches the DOM must run on the
// browser main thread - the thread the framework's UI code runs on.
//
// Browser APIs are asynchronous. Where the browser offers no synchronous
// form (storage sync, fetch, file picking, image and font loading) the
// method returns at once and reports through a callback later, from the
// main thread, between animation frames; the callback may touch UI elements.
// Version: 2.0.0
// Last Modified: 2026-09-07
// Author: UltraCanvas Framework
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <utility>

namespace UltraCanvas {

// ===== FILE SYSTEM SUPPORT =====
// The Emscripten virtual filesystem (MEMFS) is what std::filesystem, fstream
// and every framework file API see. It is lost on reload; an IDBFS mount
// persists a subtree to IndexedDB, with explicit sync points.
class WASMFileSystem {
public:
    using SyncCallback = std::function<void(bool success)>;

    // Mount an IndexedDB-backed filesystem at `mountPoint` and start loading
    // whatever it holds. Returns false if the mount itself failed; the load
    // finishes asynchronously and reports through `onLoaded`.
    static bool MountFileSystem(const std::string& mountPoint = "/data",
                                SyncCallback onLoaded = {});

    // Load IndexedDB -> mount (discards unsynced in-memory changes) and
    // save mount -> IndexedDB. Both return true when the sync was started.
    static bool SyncFromBrowser(SyncCallback onDone = {});
    static bool SyncToBrowser(SyncCallback onDone = {});

    // Plain filesystem helpers over the virtual FS.
    static bool FileExists(const std::string& path);
    static std::vector<uint8_t> ReadFile(const std::string& path);
    static bool WriteFile(const std::string& path, const std::vector<uint8_t>& data);
    static bool DeleteFile(const std::string& path);
    static bool CreateDirectory(const std::string& path);        // creates parents too
    static std::vector<std::string> ListDirectory(const std::string& path);  // names, not paths
};

// ===== NETWORK SUPPORT =====
// The page's fetch(): same-origin, or cross-origin where CORS allows. There is
// no synchronous form - a blocking fetch would freeze the tab and cannot be
// expressed without ASYNCIFY.
class WASMNetwork {
public:
    using FetchCallback = std::function<void(bool success, const std::vector<uint8_t>& data)>;
    using TextCallback = std::function<void(bool success, const std::string& text)>;

    static void FetchAsync(const std::string& url, FetchCallback callback);
    static void FetchTextAsync(const std::string& url, TextCallback callback);
};

// ===== BROWSER INTEGRATION =====
class WASMBrowser {
public:
    // Hand bytes to the browser's download manager under a suggested name -
    // the browser's "save to disk". Returns true when the download started.
    static bool DownloadFile(const std::string& filename, const std::vector<uint8_t>& data,
                             const std::string& mimeType = "application/octet-stream");
    static bool DownloadFile(const std::string& filename, const void* data, size_t size,
                             const std::string& mimeType = "application/octet-stream");

    // Open the browser's file picker. The picked files are copied into the
    // virtual filesystem under `targetDir` and the callback receives their
    // paths (empty when the user cancelled), after which the application can
    // load them with any framework file API. `accept` is the <input accept>
    // list, e.g. ".png,.jpg,image/*"; empty offers every file.
    using PickFilesCallback = std::function<void(const std::vector<std::string>& paths)>;
    static void PickFilesAsync(const std::string& accept, bool multiple,
                               PickFilesCallback callback,
                               const std::string& targetDir = "/tmp/picked");

    // Blocking browser dialogs.
    static void Alert(const std::string& message);
    static bool Confirm(const std::string& message);
    // Returns false when the prompt was dismissed; `value` is set otherwise.
    static bool Prompt(const std::string& message, const std::string& defaultValue,
                       std::string& value);
    // Convenience: empty string when dismissed.
    static std::string Prompt(const std::string& message, const std::string& defaultValue = "");

    // Console logging
    static void ConsoleLog(const std::string& message);
    static void ConsoleWarn(const std::string& message);
    static void ConsoleError(const std::string& message);

    // Browser info
    static std::string GetUserAgent();
    static std::string GetPlatform();
    static void GetScreenSize(int& width, int& height);   // the physical screen, CSS px

    // localStorage (strings, per origin, survives reloads; ~5 MB)
    static void SetLocalStorage(const std::string& key, const std::string& value);
    static std::string GetLocalStorage(const std::string& key);
    static bool HasLocalStorage(const std::string& key);
    static void RemoveLocalStorage(const std::string& key);
    static void ClearLocalStorage();
};

// ===== RESOURCE LOADING =====
class WASMResourceLoader {
public:
    // Decode an image through the browser (any format the browser decodes,
    // cross-origin only with CORS). Pixels are straight RGBA, row-major,
    // width*height*4 bytes.
    using ImageLoadCallback = std::function<void(bool success, int width, int height,
                                                 const std::vector<uint8_t>& pixels)>;
    static void LoadImage(const std::string& url, ImageLoadCallback callback);

    // Fetch a font file (TTF/OTF) into the virtual filesystem so Pango can
    // use it: on success the callback receives the path, which the app passes
    // to UltraCanvasApplication::RegisterFontFile(). (Browser-side FontFace
    // loading would not help - text is rendered by Pango from files, not by
    // the browser.)
    using FontLoadCallback = std::function<void(bool success, const std::string& fontFilePath)>;
    static void LoadFont(const std::string& fontFamily, const std::string& url,
                         FontLoadCallback callback,
                         const std::string& targetDir = "/tmp/fonts");

    // <link rel=preload> hint for an asset the page will fetch soon.
    static void PreloadAsset(const std::string& url);
};

// ===== TIME & PERFORMANCE =====
class WASMTime {
public:
    // performance.now() in seconds
    static double GetTime();

    // Performance marks (visible in the browser's performance tools)
    static void PerformanceMark(const std::string& name);
    // Duration in milliseconds between two marks, 0 when unavailable
    static double PerformanceMeasure(const std::string& name,
                                     const std::string& startMark,
                                     const std::string& endMark);
};

// ===== URL & QUERY PARAMETERS =====
class WASMURL {
public:
    static std::string GetCurrentURL();
    static std::string GetQueryParameter(const std::string& name);   // "" when absent
    static bool HasQueryParameter(const std::string& name);
    static std::vector<std::pair<std::string, std::string>> GetAllQueryParameters();
    static std::string GetHash();                                     // includes the '#'
    static void Navigate(const std::string& url);
    static void Reload();
};

} // namespace UltraCanvas
