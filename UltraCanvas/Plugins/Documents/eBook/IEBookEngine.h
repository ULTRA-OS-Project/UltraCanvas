// Plugins/Documents/eBook/IEBookEngine.h
// eBook format engine interface — chapter-oriented.
//
// An engine's job is to open a container format (EPUB, FB2, MOBI, ...) and
// hand out chapters as XHTML plus their resources (CSS, images). Rendering
// is NOT the engine's job: the viewer feeds chapter XHTML into
// HTML::ElementBuilder, which produces native UltraCanvas elements laid out
// by the CSSLayout engine.
// Version: 2.0.0
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasEBookTypes.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// One reading unit. `content` is XHTML (engines for non-HTML formats convert
// their markup to simple HTML). `href` identifies the chapter inside the
// container and is the base for resolving relative resource references.
struct EBookChapter {
    std::string title;
    std::string href;
    std::string content;
};

class IEBookEngine {
public:
    virtual ~IEBookEngine() = default;

    // ===== FORMAT =====
    virtual EBookFormat GetFormat() const = 0;
    virtual std::string GetFormatName() const = 0;
    virtual std::vector<std::string> GetSupportedExtensions() const = 0;

    // ===== LIFECYCLE =====
    virtual bool LoadFromFile(const std::string& filePath,
                              const std::string& password = "") = 0;
    virtual bool LoadFromMemory(std::vector<uint8_t> data,
                                const std::string& password = "") = 0;
    virtual void Close() = 0;
    virtual bool IsLoaded() const = 0;
    virtual std::string GetLastError() const = 0;

    // ===== DOCUMENT INFO =====
    virtual const EBookMetadata& GetMetadata() const = 0;
    virtual const std::vector<EBookTOCEntry>& GetTableOfContents() const = 0;

    // ===== CONTENT =====
    virtual int GetChapterCount() const = 0;
    // index is 0-based; returns an empty chapter when out of range.
    virtual EBookChapter GetChapter(int index) const = 0;

    // Combined author CSS for the whole book (may be empty).
    virtual std::string GetStylesheets() const = 0;

    // Raw bytes of a resource (image, font) referenced from chapter content.
    // `href` is resolved the way the chapter references it; returns an empty
    // vector when missing.
    virtual std::vector<uint8_t> GetResource(const std::string& href) const = 0;

    // ===== COVER =====
    virtual bool HasCoverImage() const = 0;
    virtual std::vector<uint8_t> GetCoverImage() const = 0;

    // ===== TEXT / SEARCH =====
    // Plain text of a chapter (for search and accessibility). Engines get a
    // tag-stripping default via EBookEngineBase.
    virtual std::string ExtractChapterText(int index) const = 0;
    virtual std::vector<EBookSearchResult> Search(const std::string& query,
                                                  bool caseSensitive = false) const = 0;

    // ===== PROGRESS =====
    // Optional; reported during long loads as (0..1, status).
    std::function<void(float, const std::string&)> onProgress;

protected:
    void ReportProgress(float progress, const std::string& status) {
        if (onProgress) onProgress(progress, status);
    }
};

// ============================================================================
// BASE IMPLEMENTATION
// ============================================================================
// Owns the common state and provides file loading, tag-stripping text
// extraction, and a chapter-walking substring search. Engines implement
// LoadFromMemory + the content accessors.

class EBookEngineBase : public IEBookEngine {
public:
    bool LoadFromFile(const std::string& filePath,
                      const std::string& password = "") override;
    void Close() override;
    bool IsLoaded() const override { return loaded; }
    std::string GetLastError() const override { return lastError; }

    const EBookMetadata& GetMetadata() const override { return metadata; }
    const std::vector<EBookTOCEntry>& GetTableOfContents() const override {
        return tableOfContents;
    }

    bool HasCoverImage() const override { return !GetCoverImage().empty(); }
    std::vector<uint8_t> GetCoverImage() const override { return {}; }
    std::string GetStylesheets() const override { return {}; }
    std::vector<uint8_t> GetResource(const std::string&) const override { return {}; }

    std::string ExtractChapterText(int index) const override;
    std::vector<EBookSearchResult> Search(const std::string& query,
                                          bool caseSensitive = false) const override;

protected:
    bool loaded = false;
    std::string lastError;
    std::string documentPath;
    EBookMetadata metadata;
    std::vector<EBookTOCEntry> tableOfContents;

    void Fail(const std::string& error) { lastError = error; }

    // Shared "close" for the base members; engines call it from Close().
    void ResetBaseState();

    static EBookFormat FormatFromExtension(const std::string& filePath);
};

// ============================================================================
// ENGINE REGISTRY
// ============================================================================
// Engines register a factory per extension; the viewer asks the registry for
// an engine matching a file. Registration happens in each engine's .cpp via
// RegisterEBookEngine so linking an engine in is all it takes.

using EBookEngineFactory = std::function<std::shared_ptr<IEBookEngine>()>;

void RegisterEBookEngine(const std::vector<std::string>& extensions,
                         EBookEngineFactory factory);

// Returns nullptr when no engine claims the extension.
std::shared_ptr<IEBookEngine> CreateEBookEngineForFile(const std::string& filePath);

std::vector<std::string> GetRegisteredEBookExtensions();

} // namespace UltraCanvas
