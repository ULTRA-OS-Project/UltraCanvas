// Plugins/Documents/eBook/EPUBEngine.h
// EPUB 2/3 format engine.
//
// An EPUB is a ZIP archive: META-INF/container.xml points at the OPF package
// document, which holds Dublin Core metadata, the manifest (all files), and
// the spine (reading order). Navigation comes from the NCX document (EPUB 2)
// or the XHTML nav document (EPUB 3). Chapters are handed out as XHTML for
// HTML::ElementBuilder; this engine performs no rendering.
// Version: 1.0.0
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework
#pragma once

#include "IEBookEngine.h"
#include "EBookArchive.h"

#include <unordered_map>

namespace UltraCanvas {

class EPUBEngine : public EBookEngineBase {
public:
    // ===== FORMAT =====
    EBookFormat GetFormat() const override { return EBookFormat::EPUB; }
    std::string GetFormatName() const override;
    std::vector<std::string> GetSupportedExtensions() const override {
        return {"epub"};
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
    std::string GetStylesheets() const override;
    std::vector<uint8_t> GetResource(const std::string& href) const override;
    std::vector<uint8_t> GetCoverImage() const override;
    // Exact spine lookup (the TOC-based default misses spine files without a
    // TOC entry, which internal links may still target).
    int GetChapterIndexForHref(const std::string& href) const override;

    // ===== PATH UTILITIES (public for tests) =====

    // Collapse "./" and "a/../" segments; keeps the path archive-relative.
    static std::string NormalizePath(const std::string& path);
    // Resolve `relative` against the directory of `baseFile` and normalize.
    // Absolute paths (leading '/') are taken as archive-relative.
    static std::string ResolveHref(const std::string& baseFile,
                                   const std::string& relative);

private:
    struct ManifestItem {
        std::string id;
        std::string href;        // archive path, normalized, OPF-relative → absolute
        std::string mediaType;
        std::string properties;  // EPUB 3 space-separated properties
    };

    struct SpineChapter {
        std::string href;        // archive path of the XHTML file
        std::string title;       // filled from the TOC when resolvable
    };

    EBookArchive archive;
    std::string opfPath;                       // archive path of the OPF
    std::vector<ManifestItem> manifest;
    std::unordered_map<std::string, size_t> manifestById;
    std::vector<SpineChapter> chapters;
    std::string coverPath;                     // archive path of cover image
    std::vector<std::string> stylesheetPaths;  // manifest CSS, spine order
    std::string versionString;                 // package/@version

    bool ParseContainer();
    bool ParseOPF(const std::string& password);
    void ParseNavigation(const std::string& ncxId, const std::string& navHref);
    void ParseNCX(const std::string& ncxPath);
    void ParseNavDoc(const std::string& navPath);
    void AssignChapterTitles();

    const ManifestItem* ItemById(const std::string& id) const;
};

// Registers the EPUB engine with the extension registry.
void RegisterEPUBEngine();

} // namespace UltraCanvas
