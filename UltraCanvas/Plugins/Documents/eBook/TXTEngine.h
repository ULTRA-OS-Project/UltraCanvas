// Plugins/Documents/eBook/TXTEngine.h
// Plain-text engine: blank-line separated paragraphs become simple HTML,
// form feeds (\f) split chapters. The simplest IEBookEngine implementation
// and the reference for writing new engines.
// Version: 1.0.0
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework
#pragma once

#include "IEBookEngine.h"

namespace UltraCanvas {

class TXTEngine : public EBookEngineBase {
public:
    // ===== FORMAT =====
    EBookFormat GetFormat() const override { return EBookFormat::TXT; }
    std::string GetFormatName() const override { return "Plain Text"; }
    std::vector<std::string> GetSupportedExtensions() const override {
        return {"txt"};
    }

    // ===== LIFECYCLE =====
    bool LoadFromMemory(std::vector<uint8_t> data,
                        const std::string& password = "") override;
    void Close() override;

    // ===== CONTENT =====
    int GetChapterCount() const override {
        return static_cast<int>(chapters.size());
    }
    EBookChapter GetChapter(int index) const override;

    // Converts one chapter's raw text to minimal HTML (public for tests).
    static std::string TextToHTML(const std::string& text);

private:
    std::vector<EBookChapter> chapters;
};

// Registers the TXT engine with the extension registry.
void RegisterTXTEngine();

// Registers every engine compiled into this library. Call once at startup
// (or before the first CreateEBookEngineForFile).
void RegisterBuiltinEBookEngines();

} // namespace UltraCanvas
