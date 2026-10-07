// Apps/UltraWeb/host/UltraWebLoader.h
// Turns what the user typed in the address bar into the bytes of a
// WebAssembly app: about:demo (built in), a local path or file:// URL, or an
// http(s):// URL fetched through UltraNet with TLS verification on. HTML
// pages are not apps; they are refused with a message until the reader
// (Phase 2 of Docs/UltraWeb/UltraWebProposal.md) exists.
//
// Load() answers on the UI thread, later, through the callback. A newer
// Load() or Cancel() makes the answer to an older one never arrive, so a
// slow server cannot replace the app the user has moved on to.
// Version: 0.1.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraWeb {

struct LoadedApp {
    bool ok = false;
    std::string address;            // what was loaded, normalised
    std::string error;              // when !ok, for the error view
    std::vector<uint8_t> module;    // .wasm bytes or WebAssembly text
};

class UltraWebLoader {
public:
    // Apps larger than this are refused.
    static constexpr int64_t kMaxModuleBytes = 64ll * 1024 * 1024;

    // "example.org/app.wasm" → "https://example.org/app.wasm"; a path that
    // exists stays a path; surrounding blanks go. Empty → about:demo.
    static std::string Normalise(const std::string& typed);
    // True for the bytes of a binary module or of WebAssembly text.
    static bool LooksLikeModule(const std::vector<uint8_t>& bytes);

    // `post` runs a task on the UI thread (PostToUIThread); file and
    // built-in loads use it too, so every answer arrives the same way.
    explicit UltraWebLoader(std::function<void(std::function<void()>)> post);
    ~UltraWebLoader();

    void Load(const std::string& typed, std::function<void(LoadedApp)> done);
    void Cancel();

    // about: pages, paths and file:// URLs, synchronously (for --check);
    // an http(s) address answers with an error here.
    static LoadedApp LoadOffline(const std::string& typed);

private:
    static LoadedApp LoadLocal(const std::string& address);

    std::function<void(std::function<void()>)> post_;
    // The navigation the answer must still belong to; shared with the
    // network callback, which may outlive this object.
    std::shared_ptr<uint64_t> current_;
};

} // namespace UltraWeb
