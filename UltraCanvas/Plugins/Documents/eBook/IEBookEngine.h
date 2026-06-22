// Plugins/Documents/eBook/IEBookEngine.h
// Abstract interface for eBook format engines (EPUB, FB2, MOBI, etc.)
// Version: 1.0.0
// Last Modified: 2025-01-08
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasEBookTypes.h"
#include "UltraCanvasCommonTypes.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace UltraCanvas {

// ===== EBOOK ENGINE INTERFACE =====
// Abstract base class for all eBook format engines
// Implementations: EPUBEngine, FB2Engine, MOBIEngine, etc.

class IEBookEngine {
public:
    virtual ~IEBookEngine() = default;
    
    // ===== DOCUMENT LIFECYCLE =====
    
    // Load document from file
    // @param filePath - Path to the eBook file
    // @param password - Optional password for encrypted documents
    // @return true if document loaded successfully
    virtual bool LoadDocument(const std::string& filePath, const std::string& password = "") = 0;
    
    // Load document from memory buffer
    // @param data - Raw file data
    // @param password - Optional password for encrypted documents
    // @return true if document loaded successfully
    virtual bool LoadDocumentFromMemory(const std::vector<uint8_t>& data, 
                                        const std::string& password = "") = 0;
    
    // Close currently loaded document and free resources
    virtual void CloseDocument() = 0;
    
    // Check if a document is currently loaded
    virtual bool IsDocumentLoaded() const = 0;
    
    // Get path to currently loaded document
    virtual std::string GetDocumentPath() const = 0;
    
    // ===== DOCUMENT INFORMATION =====
    
    // Get document metadata (title, author, etc.)
    virtual EBookMetadata GetMetadata() const = 0;
    
    // Get total page count
    virtual int GetPageCount() const = 0;
    
    // Get information about a specific page
    virtual EBookPageInfo GetPageInfo(int pageNumber) const = 0;
    
    // Get information about all pages
    virtual std::vector<EBookPageInfo> GetAllPageInfo() const = 0;
    
    // Get Table of Contents
    virtual std::vector<EBookTOCEntry> GetTableOfContents() const = 0;
    
    // Get eBook format
    virtual EBookFormat GetFormat() const = 0;
    
    // Get format version string
    virtual std::string GetFormatVersion() const = 0;
    
    // ===== PAGE RENDERING =====
    
    // Render a page to image data (RGBA pixels)
    // @param pageNumber - Page number (1-based)
    // @param settings - Render settings (DPI, zoom, colors, etc.)
    // @return Rendered page as RGBA pixel data
    virtual std::vector<uint8_t> RenderPage(int pageNumber, 
                                            const EBookRenderSettings& settings) = 0;
    
    // Render a page thumbnail
    // @param pageNumber - Page number (1-based)
    // @param maxSize - Maximum dimension (width or height)
    // @return Thumbnail as RGBA pixel data
    virtual std::vector<uint8_t> RenderPageThumbnail(int pageNumber, int maxSize = 200) = 0;
    
    // Get rendered page dimensions
    // @param pageNumber - Page number (1-based)
    // @param settings - Render settings to calculate dimensions
    // @return Page dimensions in pixels
    virtual Size2Di GetRenderedPageSize(int pageNumber, 
                                        const EBookRenderSettings& settings) const = 0;
    
    // Preload a page into cache for faster access
    // @param pageNumber - Page number (1-based)
    // @param settings - Render settings
    // @return true if preloading was successful
    virtual bool PreloadPage(int pageNumber, const EBookRenderSettings& settings) = 0;
    
    // Clear page cache to free memory
    virtual void ClearPageCache() = 0;
    
    // ===== TEXT CONTENT =====
    
    // Extract plain text from a page
    // @param pageNumber - Page number (1-based)
    // @return Plain text content of the page
    virtual std::string ExtractPageText(int pageNumber) const = 0;
    
    // Extract plain text from entire document
    // @return All text content
    virtual std::string ExtractAllText() const = 0;
    
    // Get text at specific position on page
    // @param pageNumber - Page number
    // @param x, y - Position in page coordinates
    // @return Text at that position
    virtual std::string GetTextAtPosition(int pageNumber, float x, float y) const = 0;
    
    // Get text in selection rectangle
    // @param pageNumber - Page number
    // @param rect - Selection rectangle in page coordinates
    // @return Selected text
    virtual std::string GetTextInRect(int pageNumber, const Rect2Df& rect) const = 0;
    
    // ===== SEARCH =====
    
    // Search for text in document
    // @param query - Search text
    // @param caseSensitive - Case sensitive search
    // @param wholeWord - Match whole words only
    // @return List of search results
    virtual std::vector<EBookSearchResult> SearchText(const std::string& query,
                                                      bool caseSensitive = false,
                                                      bool wholeWord = false) const = 0;
    
    // Search for text in specific page
    virtual std::vector<EBookSearchResult> SearchTextInPage(int pageNumber,
                                                            const std::string& query,
                                                            bool caseSensitive = false) const = 0;
    
    // ===== NAVIGATION =====
    
    // Get chapter count
    virtual int GetChapterCount() const = 0;
    
    // Get chapter information
    virtual EBookTOCEntry GetChapterInfo(int chapterIndex) const = 0;
    
    // Get page number for chapter
    // @param chapterIndex - Chapter index (0-based)
    // @return First page of chapter (1-based)
    virtual int GetChapterStartPage(int chapterIndex) const = 0;
    
    // Get chapter index for page
    // @param pageNumber - Page number (1-based)
    // @return Chapter index (0-based), -1 if not found
    virtual int GetChapterForPage(int pageNumber) const = 0;
    
    // Navigate to internal link/reference
    // @param href - Internal link reference
    // @return Page number that link points to
    virtual int ResolveLink(const std::string& href) const = 0;
    
    // ===== REFLOW SUPPORT =====
    
    // Check if engine supports text reflow
    virtual bool SupportsReflow() const = 0;
    
    // Get reflowed page content as HTML/XHTML
    // @param pageNumber - Page number
    // @param settings - Reflow settings
    // @return Reflowed content as HTML
    virtual std::string GetReflowedContent(int pageNumber,
                                           const EBookReflowSettings& settings) const = 0;
    
    // Calculate page breaks for reflowed content
    // @param settings - Reflow settings
    // @param viewportSize - Display area size
    // @return Total pages after reflow
    virtual int CalculateReflowPages(const EBookReflowSettings& settings,
                                     const Size2Di& viewportSize) const = 0;
    
    // ===== COVER IMAGE =====
    
    // Check if document has cover image
    virtual bool HasCoverImage() const = 0;
    
    // Get cover image as RGBA pixel data
    // @return Cover image data
    virtual std::vector<uint8_t> GetCoverImage() const = 0;
    
    // Get cover image dimensions
    virtual Size2Di GetCoverImageSize() const = 0;
    
    // ===== EMBEDDED RESOURCES =====
    
    // Get list of embedded images
    virtual std::vector<std::string> GetEmbeddedImages() const = 0;
    
    // Get embedded image by ID
    // @param imageId - Image identifier
    // @return Image data as RGBA pixels
    virtual std::vector<uint8_t> GetEmbeddedImage(const std::string& imageId) const = 0;
    
    // Get list of embedded fonts
    virtual std::vector<std::string> GetEmbeddedFonts() const = 0;
    
    // Get embedded font data
    virtual std::vector<uint8_t> GetEmbeddedFont(const std::string& fontId) const = 0;
    
    // Get list of embedded stylesheets
    virtual std::vector<std::string> GetStylesheets() const = 0;
    
    // Get stylesheet content
    virtual std::string GetStylesheetContent(const std::string& stylesheetId) const = 0;
    
    // ===== ENGINE INFORMATION =====
    
    // Get engine name
    virtual std::string GetEngineName() const = 0;
    
    // Get engine version
    virtual std::string GetEngineVersion() const = 0;
    
    // Get supported file extensions
    virtual std::vector<std::string> GetSupportedExtensions() const = 0;
    
    // Get engine capabilities
    virtual EBookPluginCapabilities GetCapabilities() const = 0;
    
    // ===== VALIDATION =====
    
    // Validate file before loading
    // @param filePath - Path to file
    // @return true if file appears valid
    virtual bool ValidateFile(const std::string& filePath) const = 0;
    
    // Check if password is required
    // @param filePath - Path to file
    // @return true if document is encrypted
    virtual bool RequiresPassword(const std::string& filePath) const = 0;
    
    // Verify password for encrypted document
    // @param password - Password to verify
    // @return true if password is correct
    virtual bool VerifyPassword(const std::string& password) const = 0;
    
    // ===== CALLBACKS =====
    
    // Set progress callback for long operations
    std::function<void(float progress, const std::string& status)> onProgress;
    
    // Set error callback
    std::function<void(const std::string& error)> onError;
    
protected:
    // Helper method to emit progress
    void EmitProgress(float progress, const std::string& status) {
        if (onProgress) onProgress(progress, status);
    }
    
    // Helper method to emit error
    void EmitError(const std::string& error) {
        if (onError) onError(error);
    }
};

// ===== ENGINE FACTORY INTERFACE =====
// Factory for creating eBook engines based on format

class IEBookEngineFactory {
public:
    virtual ~IEBookEngineFactory() = default;
    
    // Create engine for specific format
    virtual std::shared_ptr<IEBookEngine> CreateEngine(EBookFormat format) const = 0;
    
    // Create engine for file extension
    virtual std::shared_ptr<IEBookEngine> CreateEngineForExtension(
        const std::string& extension) const = 0;
    
    // Create engine for file (auto-detect format)
    virtual std::shared_ptr<IEBookEngine> CreateEngineForFile(
        const std::string& filePath) const = 0;
    
    // Get list of supported formats
    virtual std::vector<EBookFormat> GetSupportedFormats() const = 0;
    
    // Get list of supported extensions
    virtual std::vector<std::string> GetSupportedExtensions() const = 0;
    
    // Check if format is supported
    virtual bool IsFormatSupported(EBookFormat format) const = 0;
    
    // Check if extension is supported
    virtual bool IsExtensionSupported(const std::string& extension) const = 0;
};

// ===== BASE ENGINE IMPLEMENTATION =====
// Provides common functionality for all engines

class EBookEngineBase : public IEBookEngine {
protected:
    std::string documentPath;
    bool documentLoaded = false;
    EBookMetadata metadata;
    std::vector<EBookPageInfo> pageInfos;
    std::vector<EBookTOCEntry> tableOfContents;
    
    // Page cache (page number -> rendered data)
    std::unordered_map<int, std::vector<uint8_t>> pageCache;
    int maxCacheSize = 10;
    
public:
    bool IsDocumentLoaded() const override { return documentLoaded; }
    std::string GetDocumentPath() const override { return documentPath; }
    EBookMetadata GetMetadata() const override { return metadata; }
    
    int GetPageCount() const override {
        return static_cast<int>(pageInfos.size());
    }
    
    EBookPageInfo GetPageInfo(int pageNumber) const override {
        if (pageNumber < 1 || pageNumber > GetPageCount()) {
            return EBookPageInfo();
        }
        return pageInfos[pageNumber - 1];
    }
    
    std::vector<EBookPageInfo> GetAllPageInfo() const override {
        return pageInfos;
    }
    
    std::vector<EBookTOCEntry> GetTableOfContents() const override {
        return tableOfContents;
    }
    
    int GetChapterCount() const override {
        return static_cast<int>(tableOfContents.size());
    }
    
    EBookTOCEntry GetChapterInfo(int chapterIndex) const override {
        if (chapterIndex < 0 || chapterIndex >= GetChapterCount()) {
            return EBookTOCEntry();
        }
        return tableOfContents[chapterIndex];
    }
    
    void ClearPageCache() override {
        pageCache.clear();
    }
    
protected:
    // Cache management
    void AddToCache(int pageNumber, const std::vector<uint8_t>& data) {
        if (pageCache.size() >= static_cast<size_t>(maxCacheSize)) {
            // Remove oldest entry (simple LRU approximation)
            if (!pageCache.empty()) {
                pageCache.erase(pageCache.begin());
            }
        }
        pageCache[pageNumber] = data;
    }
    
    bool GetFromCache(int pageNumber, std::vector<uint8_t>& data) const {
        auto it = pageCache.find(pageNumber);
        if (it != pageCache.end()) {
            data = it->second;
            return true;
        }
        return false;
    }
    
    bool IsInCache(int pageNumber) const {
        return pageCache.find(pageNumber) != pageCache.end();
    }
    
    // Helper to detect format from file extension
    static EBookFormat DetectFormatFromExtension(const std::string& filePath) {
        size_t dotPos = filePath.find_last_of('.');
        if (dotPos == std::string::npos) return EBookFormat::Unknown;
        
        std::string ext = filePath.substr(dotPos + 1);
        // Convert to lowercase
        for (auto& c : ext) c = static_cast<char>(std::tolower(c));
        
        if (ext == "epub") return EBookFormat::EPUB;
        if (ext == "fb2") return EBookFormat::FB2;
        if (ext == "mobi") return EBookFormat::MOBI;
        if (ext == "azw") return EBookFormat::AZW;
        if (ext == "azw3") return EBookFormat::AZW3;
        if (ext == "kfx") return EBookFormat::KFX;
        if (ext == "pml") return EBookFormat::PML;
        if (ext == "pdb") return EBookFormat::PDB;
        if (ext == "lrf") return EBookFormat::LRF;
        if (ext == "lrx") return EBookFormat::LRX;
        if (ext == "lit") return EBookFormat::LIT;
        if (ext == "rb") return EBookFormat::RB;
        if (ext == "tcr") return EBookFormat::TCR;
        if (ext == "oeb") return EBookFormat::OEB;
        if (ext == "pages") return EBookFormat::PAGES;
        if (ext == "ibooks") return EBookFormat::IBOOKS;
        if (ext == "cbz") return EBookFormat::CBZ;
        if (ext == "cbr") return EBookFormat::CBR;
        if (ext == "cb7") return EBookFormat::CB7;
        if (ext == "txt") return EBookFormat::TXT;
        if (ext == "rtf") return EBookFormat::RTF;
        if (ext == "html" || ext == "htm") return EBookFormat::HTML;
        if (ext == "xhtml") return EBookFormat::XHTML;
        
        return EBookFormat::Unknown;
    }
    
    // Helper to get format name
    static std::string GetFormatName(EBookFormat format) {
        switch (format) {
            case EBookFormat::EPUB: return "EPUB";
            case EBookFormat::EPUB2: return "EPUB 2";
            case EBookFormat::EPUB3: return "EPUB 3";
            case EBookFormat::FB2: return "FictionBook 2";
            case EBookFormat::FB3: return "FictionBook 3";
            case EBookFormat::MOBI: return "Mobipocket";
            case EBookFormat::AZW: return "Amazon Kindle";
            case EBookFormat::AZW3: return "Amazon KF8";
            case EBookFormat::KFX: return "Amazon KFX";
            case EBookFormat::PML: return "Palm Markup";
            case EBookFormat::PDB: return "Palm Database";
            case EBookFormat::LRF: return "Sony Reader";
            case EBookFormat::LRX: return "Sony BBeB";
            case EBookFormat::LIT: return "Microsoft LIT";
            case EBookFormat::RB: return "RocketEdition";
            case EBookFormat::TCR: return "Psion Text";
            case EBookFormat::OEB: return "Open eBook";
            case EBookFormat::PAGES: return "Apple Pages";
            case EBookFormat::IBOOKS: return "Apple iBooks";
            case EBookFormat::CBZ: return "Comic Book ZIP";
            case EBookFormat::CBR: return "Comic Book RAR";
            case EBookFormat::CB7: return "Comic Book 7z";
            case EBookFormat::TXT: return "Plain Text";
            case EBookFormat::RTF: return "Rich Text";
            case EBookFormat::HTML: return "HTML";
            case EBookFormat::XHTML: return "XHTML";
            default: return "Unknown";
        }
    }
};

// ===== FORWARD DECLARATIONS FOR ENGINE IMPLEMENTATIONS =====
class EPUBEngine;
class FB2Engine;
class MOBIEngine;
class CBZEngine;
class PlainTextEngine;

// ===== FACTORY FUNCTION DECLARATIONS =====

// Create default engine factory
std::shared_ptr<IEBookEngineFactory> CreateEBookEngineFactory();

// Create specific engines
std::shared_ptr<IEBookEngine> CreateEPUBEngine();
std::shared_ptr<IEBookEngine> CreateFB2Engine();
std::shared_ptr<IEBookEngine> CreateMOBIEngine();
std::shared_ptr<IEBookEngine> CreateCBZEngine();
std::shared_ptr<IEBookEngine> CreatePlainTextEngine();

// Auto-detect and create appropriate engine for file
std::shared_ptr<IEBookEngine> CreateEBookEngineForFile(const std::string& filePath);

} // namespace UltraCanvas
