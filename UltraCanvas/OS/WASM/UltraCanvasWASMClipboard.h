// OS/WASM/UltraCanvasWASMClipboard.h
// WebAssembly clipboard backend: the browser's asynchronous Clipboard API
// behind the framework's synchronous UltraCanvasClipboardBackend contract.
//
// The browser never lets a page read the clipboard synchronously, so the
// backend keeps a text cache and refreshes it from every source the browser
// does offer:
//
//   * the DOM `paste` event - UltraCanvasWASMApplication lets Ctrl/Cmd+V reach
//     the browser instead of consuming it, the browser fires `paste` with the
//     clipboard text attached, and the application hands that text to
//     OfferText() before it queues the Ctrl+V key event. By the time a text
//     field asks GetClipboardText() the cache holds exactly what the user is
//     pasting;
//   * navigator.clipboard.readText(), started by GetClipboardText() and
//     Initialize() where the browser allows it (Chromium after a one-time
//     permission prompt; Firefox and Safari mostly refuse). Its result lands
//     in the cache for the *next* read, which covers menu-driven Paste;
//   * SetClipboardText(), which fills the cache and forwards the text to
//     navigator.clipboard.writeText() so it reaches the system clipboard.
//
// Text only: images and file lists report unsupported. Main-thread only, like
// everything that touches the DOM under this backend.
// Version: 1.0.0
// Last Modified: 2026-09-07
// Author: UltraCanvas Framework

#pragma once

#ifndef ULTRACANVAS_WASM_CLIPBOARD_H
#define ULTRACANVAS_WASM_CLIPBOARD_H

#include "UltraCanvasClipboard.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {

    class UltraCanvasWASMClipboard : public UltraCanvasClipboardBackend {
    public:
        UltraCanvasWASMClipboard() = default;
        ~UltraCanvasWASMClipboard() override;

        bool Initialize() override;
        void Shutdown() override;

        bool GetClipboardText(std::string& text) override;
        bool SetClipboardText(const std::string& text) override;

        // Non-text formats: unsupported on this backend (see header comment).
        bool GetClipboardImage(std::vector<uint8_t>&, std::string&) override { return false; }
        bool SetClipboardImage(const std::vector<uint8_t>&, const std::string&) override { return false; }
        bool GetClipboardFiles(std::vector<std::string>&) override { return false; }
        bool SetClipboardFiles(const std::vector<std::string>&) override { return false; }

        bool HasClipboardChanged() override;
        void ResetChangeState() override;

        std::vector<std::string> GetAvailableFormats() override;
        bool IsFormatAvailable(const std::string& format) override;

        // Text the browser handed over (a `paste` event, or a completed
        // readText()). Safe to call when no backend is alive: it is dropped.
        static void OfferText(const std::string& text);

    private:
        static UltraCanvasWASMClipboard* instance;

        // Ask the browser for the current clipboard text; the answer, if any,
        // arrives later through OfferText().
        void RequestAsyncRefresh();

        std::string cachedText;
        bool hasText = false;
        uint64_t generation = 0;      // bumped whenever the cached text changes
        uint64_t seenGeneration = 0;  // generation at the last ResetChangeState()
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_WASM_CLIPBOARD_H
