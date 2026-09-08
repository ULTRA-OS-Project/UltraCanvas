// OS/WASM/UltraCanvasWASMClipboard.cpp
// WebAssembly clipboard backend - see the header for the design.
// Version: 1.0.0
// Last Modified: 2026-09-07
// Author: UltraCanvas Framework

#include "UltraCanvasWASMClipboard.h"
#include "UltraCanvasDebug.h"

#include <emscripten.h>

#include <cstdlib>

// navigator.clipboard.readText() resolves into this: the JS side allocates
// the UTF-8 copy with stringToNewUTF8(), ownership passes to us.
extern "C" EMSCRIPTEN_KEEPALIVE void UltraCanvasWasmClipboardReadText(char* text) {
    if (!text) return;
    UltraCanvas::UltraCanvasWASMClipboard::OfferText(text);
    free(text);
}

namespace UltraCanvas {

    UltraCanvasWASMClipboard* UltraCanvasWASMClipboard::instance = nullptr;

    UltraCanvasWASMClipboard::~UltraCanvasWASMClipboard() {
        Shutdown();
    }

    bool UltraCanvasWASMClipboard::Initialize() {
        instance = this;
        cachedText.clear();
        hasText = false;
        generation = 0;
        seenGeneration = 0;
        // Prime the cache where the browser lets us; harmless where it does
        // not (the promise rejects and nothing happens).
        RequestAsyncRefresh();
        return true;
    }

    void UltraCanvasWASMClipboard::Shutdown() {
        if (instance == this) {
            instance = nullptr;
        }
    }

    void UltraCanvasWASMClipboard::OfferText(const std::string& text) {
        auto* self = instance;
        if (!self) return;
        if (self->hasText && self->cachedText == text) return;
        self->cachedText = text;
        self->hasText = true;
        ++self->generation;
    }

    void UltraCanvasWASMClipboard::RequestAsyncRefresh() {
        EM_ASM({
            try {
                if (!navigator.clipboard || !navigator.clipboard.readText) return;
                navigator.clipboard.readText().then(function(text) {
                    if (typeof text !== 'string') return;
                    _UltraCanvasWasmClipboardReadText(stringToNewUTF8(text));
                }).catch(function() { /* denied, unfocused, or unsupported */ });
            } catch (e) { /* no Clipboard API */ }
        });
    }

    bool UltraCanvasWASMClipboard::GetClipboardText(std::string& text) {
        // Whatever the browser last told us, and a request for what it holds
        // now - the answer to which serves the next call.
        RequestAsyncRefresh();
        if (!hasText) return false;
        text = cachedText;
        return true;
    }

    bool UltraCanvasWASMClipboard::SetClipboardText(const std::string& text) {
        if (!hasText || cachedText != text) {
            cachedText = text;
            hasText = true;
            ++generation;
        }
        // writeText needs a secure context and (in most browsers) transient
        // user activation - a Ctrl+C handled within the same frame qualifies.
        // Failure leaves the text in the cache, so in-app paste still works.
        EM_ASM({
            try {
                if (navigator.clipboard && navigator.clipboard.writeText) {
                    navigator.clipboard.writeText(UTF8ToString($0)).catch(function(e) {
                        console.warn('UltraCanvas WASM: clipboard write refused:', e);
                    });
                }
            } catch (e) { /* no Clipboard API */ }
        }, text.c_str());
        return true;
    }

    bool UltraCanvasWASMClipboard::HasClipboardChanged() {
        return generation != seenGeneration;
    }

    void UltraCanvasWASMClipboard::ResetChangeState() {
        seenGeneration = generation;
    }

    std::vector<std::string> UltraCanvasWASMClipboard::GetAvailableFormats() {
        std::vector<std::string> formats;
        if (hasText) formats.push_back("text/plain");
        return formats;
    }

    bool UltraCanvasWASMClipboard::IsFormatAvailable(const std::string& format) {
        return hasText && format == "text/plain";
    }

} // namespace UltraCanvas
