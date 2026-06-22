// Plugins/Documents/eBook/EPUBEngine.h
// EPUB format engine implementation with full EPUB 2.x and 3.x support
// Version: 1.0.0
// Last Modified: 2025-01-08
// Author: UltraCanvas Framework
#pragma once

#include "IEBookEngine.h"
#include "UltraCanvasRenderContext.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>

// Conditional compilation for EPUB support
#ifdef ULTRACANVAS_EPUB_SUPPORT

// Forward declarations for third-party libraries
// Using: libzip for archive handling, pugixml for XML parsing
// Alternative: miniz + custom XML parser for minimal dependencies

namespace UltraCanvas {

// ===== EPUB INTERNAL STRUCTURES =====

// EPUB Manifest Item
struct EPUBManifestItem {
    std::string id;
    std::string href;
    std::string mediaType;
    std::string properties;     // EPUB 3 properties
    std::string fallback;
    bool isSpine = false;
    bool isNav = false;
    bool isCover = false;
    bool isNCX = false;
    
    bool IsXHTML() const {
        return mediaType == "application/xhtml+xml" || 
               mediaType == "text/html";
    }
    
    bool IsImage() const {
        return mediaType.find("image/") == 0;
    }
    
    bool IsCSS() const {
        return mediaType == "text/css";
    }
    
    bool IsFont() const {
        return mediaType.find("font/") == 0 ||
               mediaType == "application/font-woff" ||
               mediaType == "application/font-woff2" ||
               mediaType == "application/vnd.ms-opentype" ||
               mediaType == "application/x-font-ttf" ||
               mediaType == "application/x-font-otf";
    }
};

// EPUB Spine Item
struct EPUBSpineItem {
    std::string idref;
    std::string id;
    bool linear = true;
    std::string properties;
    int pageStart = 0;
    int pageCount = 0;
    
    // Reference to manifest item
    const EPUBManifestItem* manifestItem = nullptr;
};

// EPUB Navigation Point (NCX or Nav document)
struct EPUBNavPoint {
    std::string id;
    std::string label;
    std::string content;        // href to content
    int playOrder = 0;
    int depth = 0;
    std::vector<EPUBNavPoint> children;
    
    int GetTotalPoints() const {
        int count = 1;
        for (const auto& child : children) {
            count += child.GetTotalPoints();
        }
        return count;
    }
};

// EPUB Package Document (OPF)
struct EPUBPackage {
    // Package attributes
    std::string uniqueIdentifier;
    std::string version;            // "2.0" or "3.0"
    std::string xmlLang;
    std::string direction;          // "ltr" or "rtl"
    
    // Metadata
    std::string title;
    std::string language;
    std::string identifier;
    std::vector<std::string> creators;
    std::vector<std::string> contributors;
    std::string publisher;
    std::string date;
    std::string description;
    std::string rights;
    std::string source;
    std::vector<std::string> subjects;
    std::string type;
    std::string format;
    std::string coverage;
    std::string relation;
    
    // EPUB 3 metadata
    std::string modifiedDate;
    std::string belongsToCollection;
    int seriesIndex = 0;
    
    // Custom metadata (dc:meta elements)
    std::unordered_map<std::string, std::string> customMeta;
    
    // Manifest
    std::vector<EPUBManifestItem> manifest;
    std::unordered_map<std::string, size_t> manifestById;
    
    // Spine
    std::vector<EPUBSpineItem> spine;
    std::string tocId;              // NCX id for EPUB 2
    std::string pageProgressionDir; // "ltr" or "rtl"
    
    // Navigation
    std::vector<EPUBNavPoint> navPoints;
    
    // Cover
    std::string coverId;
    std::string coverImageId;
    
    bool IsEPUB3() const { return version.find("3") == 0; }
    bool IsRTL() const { return pageProgressionDir == "rtl" || direction == "rtl"; }
    
    const EPUBManifestItem* GetManifestItem(const std::string& id) const {
        auto it = manifestById.find(id);
        if (it != manifestById.end() && it->second < manifest.size()) {
            return &manifest[it->second];
        }
        return nullptr;
    }
};

// EPUB Content Document (parsed XHTML)
struct EPUBContentDocument {
    std::string id;
    std::string href;
    std::string title;
    std::string content;            // Raw XHTML content
    std::string plainText;          // Extracted plain text
    std::vector<std::string> images; // Referenced images
    std::vector<std::string> links; // Internal links
    int estimatedPages = 1;
    
    // Rendered pages
    struct RenderedPage {
        int pageNumber;
        std::vector<uint8_t> imageData;
        int width;
        int height;
    };
    std::vector<RenderedPage> renderedPages;
};

// ===== EPUB ENGINE IMPLEMENTATION =====

class EPUBEngine : public EBookEngineBase {
public:
    EPUBEngine();
    virtual ~EPUBEngine();
    
    // ===== IEBookEngine Implementation =====
    
    // Document lifecycle
    bool LoadDocument(const std::string& filePath, 
                      const std::string& password = "") override;
    bool LoadDocumentFromMemory(const std::vector<uint8_t>& data,
                                const std::string& password = "") override;
    void CloseDocument() override;
    
    // Format info
    EBookFormat GetFormat() const override;
    std::string GetFormatVersion() const override;
    
    // Page rendering
    std::vector<uint8_t> RenderPage(int pageNumber,
                                    const EBookRenderSettings& settings) override;
    std::vector<uint8_t> RenderPageThumbnail(int pageNumber, int maxSize = 200) override;
    Size2Di GetRenderedPageSize(int pageNumber,
                                const EBookRenderSettings& settings) const override;
    bool PreloadPage(int pageNumber, const EBookRenderSettings& settings) override;
    
    // Text content
    std::string ExtractPageText(int pageNumber) const override;
    std::string ExtractAllText() const override;
    std::string GetTextAtPosition(int pageNumber, float x, float y) const override;
    std::string GetTextInRect(int pageNumber, const Rect2Df& rect) const override;
    
    // Search
    std::vector<EBookSearchResult> SearchText(const std::string& query,
                                              bool caseSensitive = false,
                                              bool wholeWord = false) const override;
    std::vector<EBookSearchResult> SearchTextInPage(int pageNumber,
                                                    const std::string& query,
                                                    bool caseSensitive = false) const override;
    
    // Navigation
    int GetChapterStartPage(int chapterIndex) const override;
    int GetChapterForPage(int pageNumber) const override;
    int ResolveLink(const std::string& href) const override;
    
    // Reflow
    bool SupportsReflow() const override { return true; }
    std::string GetReflowedContent(int pageNumber,
                                   const EBookReflowSettings& settings) const override;
    int CalculateReflowPages(const EBookReflowSettings& settings,
                             const Size2Di& viewportSize) const override;
    
    // Cover
    bool HasCoverImage() const override;
    std::vector<uint8_t> GetCoverImage() const override;
    Size2Di GetCoverImageSize() const override;
    
    // Resources
    std::vector<std::string> GetEmbeddedImages() const override;
    std::vector<uint8_t> GetEmbeddedImage(const std::string& imageId) const override;
    std::vector<std::string> GetEmbeddedFonts() const override;
    std::vector<uint8_t> GetEmbeddedFont(const std::string& fontId) const override;
    std::vector<std::string> GetStylesheets() const override;
    std::string GetStylesheetContent(const std::string& stylesheetId) const override;
    
    // Engine info
    std::string GetEngineName() const override { return "UltraCanvas EPUB Engine"; }
    std::string GetEngineVersion() const override { return "1.0.0"; }
    std::vector<std::string> GetSupportedExtensions() const override {
        return {"epub"};
    }
    EBookPluginCapabilities GetCapabilities() const override;
    
    // Validation
    bool ValidateFile(const std::string& filePath) const override;
    bool RequiresPassword(const std::string& filePath) const override;
    bool VerifyPassword(const std::string& password) const override;
    
    // ===== EPUB-Specific Methods =====
    
    // Get raw XHTML content of a spine item
    std::string GetSpineItemContent(int spineIndex) const;
    
    // Get CSS content combined from all stylesheets
    std::string GetCombinedCSS() const;
    
    // Get the package document info
    const EPUBPackage& GetPackage() const { return package; }
    
    // Check EPUB version
    bool IsEPUB3() const { return package.IsEPUB3(); }
    bool IsEPUB2() const { return !package.IsEPUB3(); }
    
    // Get spine item count
    int GetSpineItemCount() const { return static_cast<int>(package.spine.size()); }
    
private:
    // ===== Internal Data =====
    EPUBPackage package;
    std::vector<EPUBContentDocument> contentDocuments;
    std::unordered_map<std::string, std::vector<uint8_t>> resourceCache;
    
    // Archive handling
    void* zipArchive = nullptr;     // libzip archive handle
    std::vector<uint8_t> archiveData; // For memory-loaded documents
    std::string containerPath;      // Path to container.xml
    std::string opfPath;            // Path to OPF file
    std::string opfDirectory;       // Directory containing OPF
    
    // Rendering
    mutable std::mutex renderMutex;
    EBookRenderSettings lastRenderSettings;
    
    // Page mapping
    struct PageMapping {
        int spineIndex;
        int pageInDocument;
        int globalPage;
    };
    std::vector<PageMapping> pageMappings;
    
    // ===== Internal Methods =====
    
    // Archive operations
    bool OpenArchive(const std::string& filePath);
    bool OpenArchiveFromMemory(const std::vector<uint8_t>& data);
    void CloseArchive();
    std::vector<uint8_t> ReadFileFromArchive(const std::string& path) const;
    std::string ReadTextFileFromArchive(const std::string& path) const;
    bool FileExistsInArchive(const std::string& path) const;
    std::vector<std::string> ListFilesInArchive() const;
    
    // Parsing
    bool ParseContainer();
    bool ParseOPF();
    bool ParseMetadata(void* metadataNode);
    bool ParseManifest(void* manifestNode);
    bool ParseSpine(void* spineNode);
    bool ParseNCX();
    bool ParseNav();
    void BuildNavPoints(void* navNode, std::vector<EPUBNavPoint>& points, int depth);
    
    // Content processing
    bool LoadContentDocuments();
    bool ProcessContentDocument(const EPUBSpineItem& spineItem, 
                                EPUBContentDocument& doc);
    std::string ExtractPlainText(const std::string& xhtml) const;
    std::vector<std::string> ExtractImageReferences(const std::string& xhtml) const;
    
    // Page calculation
    void CalculatePages(const EBookRenderSettings& settings);
    int EstimatePageCount(const std::string& text, 
                          const EBookRenderSettings& settings) const;
    
    // Rendering helpers
    std::vector<uint8_t> RenderXHTMLToImage(const std::string& xhtml,
                                            const EBookRenderSettings& settings,
                                            int pageInDoc = 0);
    std::vector<uint8_t> RenderTextToImage(const std::string& text,
                                           const EBookRenderSettings& settings);
    
    // Path resolution
    std::string ResolvePath(const std::string& relativePath) const;
    std::string GetAbsolutePath(const std::string& href, 
                                const std::string& basePath) const;
    
    // Metadata helpers
    void BuildMetadata();
    void BuildTableOfContents();
    void BuildPageInfos();
    
    // Cover detection
    std::string FindCoverImage() const;
};

// ===== FACTORY FUNCTION =====

inline std::shared_ptr<IEBookEngine> CreateEPUBEngine() {
    return std::make_shared<EPUBEngine>();
}

// ===== EPUB PLUGIN CLASS =====
// Integrates EPUBEngine with UltraCanvas plugin system

class UltraCanvasEPUBPlugin {
public:
    UltraCanvasEPUBPlugin();
    ~UltraCanvasEPUBPlugin() = default;
    
    // Plugin information
    std::string GetPluginName() const { return "UltraCanvas EPUB Plugin"; }
    std::string GetPluginVersion() const { return "1.0.0"; }
    std::vector<std::string> GetSupportedExtensions() const { return {"epub"}; }
    
    // File handling
    bool CanHandle(const std::string& filePath) const;
    bool ValidateFile(const std::string& filePath) const;
    
    // Engine creation
    std::shared_ptr<EPUBEngine> CreateEngine() const {
        return std::make_shared<EPUBEngine>();
    }
    
    // Get capabilities
    EBookPluginCapabilities GetCapabilities() const;
    
private:
    std::shared_ptr<EPUBEngine> engine;
};

// ===== REGISTRATION FUNCTIONS =====

void RegisterEPUBPlugin();
void UnregisterEPUBPlugin();

} // namespace UltraCanvas

#else // ULTRACANVAS_EPUB_SUPPORT not defined

namespace UltraCanvas {

// Stub implementation when EPUB support is disabled
class EPUBEngine : public EBookEngineBase {
public:
    EPUBEngine() {
        std::cerr << "EPUB Engine: Not compiled with EPUB support" << std::endl;
    }
    
    bool LoadDocument(const std::string&, const std::string& = "") override { 
        return false; 
    }
    bool LoadDocumentFromMemory(const std::vector<uint8_t>&, 
                                const std::string& = "") override { 
        return false; 
    }
    void CloseDocument() override {}
    
    EBookFormat GetFormat() const override { return EBookFormat::EPUB; }
    std::string GetFormatVersion() const override { return ""; }
    
    std::vector<uint8_t> RenderPage(int, const EBookRenderSettings&) override { 
        return {}; 
    }
    std::vector<uint8_t> RenderPageThumbnail(int, int = 200) override { 
        return {}; 
    }
    Size2Di GetRenderedPageSize(int, const EBookRenderSettings&) const override { 
        return {0, 0}; 
    }
    bool PreloadPage(int, const EBookRenderSettings&) override { return false; }
    
    std::string ExtractPageText(int) const override { return ""; }
    std::string ExtractAllText() const override { return ""; }
    std::string GetTextAtPosition(int, float, float) const override { return ""; }
    std::string GetTextInRect(int, const Rect2Df&) const override { return ""; }
    
    std::vector<EBookSearchResult> SearchText(const std::string&, bool, bool) const override { 
        return {}; 
    }
    std::vector<EBookSearchResult> SearchTextInPage(int, const std::string&, bool) const override { 
        return {}; 
    }
    
    int GetChapterStartPage(int) const override { return 0; }
    int GetChapterForPage(int) const override { return -1; }
    int ResolveLink(const std::string&) const override { return 0; }
    
    bool SupportsReflow() const override { return false; }
    std::string GetReflowedContent(int, const EBookReflowSettings&) const override { 
        return ""; 
    }
    int CalculateReflowPages(const EBookReflowSettings&, const Size2Di&) const override { 
        return 0; 
    }
    
    bool HasCoverImage() const override { return false; }
    std::vector<uint8_t> GetCoverImage() const override { return {}; }
    Size2Di GetCoverImageSize() const override { return {0, 0}; }
    
    std::vector<std::string> GetEmbeddedImages() const override { return {}; }
    std::vector<uint8_t> GetEmbeddedImage(const std::string&) const override { return {}; }
    std::vector<std::string> GetEmbeddedFonts() const override { return {}; }
    std::vector<uint8_t> GetEmbeddedFont(const std::string&) const override { return {}; }
    std::vector<std::string> GetStylesheets() const override { return {}; }
    std::string GetStylesheetContent(const std::string&) const override { return ""; }
    
    std::string GetEngineName() const override { return "EPUB Engine (Disabled)"; }
    std::string GetEngineVersion() const override { return "0.0.0"; }
    std::vector<std::string> GetSupportedExtensions() const override { return {}; }
    EBookPluginCapabilities GetCapabilities() const override { return {}; }
    
    bool ValidateFile(const std::string&) const override { return false; }
    bool RequiresPassword(const std::string&) const override { return false; }
    bool VerifyPassword(const std::string&) const override { return false; }
};

inline std::shared_ptr<IEBookEngine> CreateEPUBEngine() {
    std::cerr << "EPUB Engine: Cannot create - not compiled with EPUB support" << std::endl;
    return nullptr;
}

inline void RegisterEPUBPlugin() {
    std::cerr << "EPUB Plugin: Cannot register - not compiled with EPUB support" << std::endl;
}

inline void UnregisterEPUBPlugin() {}

} // namespace UltraCanvas

#endif // ULTRACANVAS_EPUB_SUPPORT

/*
=== EPUB ENGINE FEATURES ===

✅ **Full EPUB 2.x and 3.x Support**:
- OPF package document parsing
- NCX navigation (EPUB 2) and Nav document (EPUB 3)
- Manifest and spine processing
- Metadata extraction (Dublin Core + EPUB 3 extensions)

✅ **Content Rendering**:
- XHTML content document rendering
- CSS stylesheet support
- Embedded image handling
- Font embedding support
- Page layout with configurable margins

✅ **Navigation Features**:
- Table of Contents with nested chapters
- Internal link resolution
- Chapter navigation
- Page-to-chapter mapping

✅ **Text Features**:
- Full text extraction
- Text search with highlighting
- Text reflow with customizable settings
- Hyphenation support

✅ **Resource Management**:
- ZIP archive handling (libzip or miniz)
- Resource caching for performance
- Embedded font extraction
- Stylesheet merging

✅ **Build Configuration**:
```cmake
# Enable EPUB support
set(ULTRACANVAS_EPUB_SUPPORT ON)

# Using libzip
find_package(LibZip REQUIRED)
target_link_libraries(UltraCanvas LibZip::LibZip)

# Using pugixml for XML parsing
find_package(pugixml REQUIRED)
target_link_libraries(UltraCanvas pugixml::pugixml)

target_compile_definitions(UltraCanvas PRIVATE ULTRACANVAS_EPUB_SUPPORT)
```

✅ **Dependencies**:
- libzip or miniz (ZIP archive handling)
- pugixml or tinyxml2 (XML parsing)
- Cairo/Pango (XHTML rendering) or custom renderer
- FreeType (font rendering)

✅ **Usage Example**:
```cpp
auto engine = UltraCanvas::CreateEPUBEngine();

if (engine->LoadDocument("book.epub")) {
    auto metadata = engine->GetMetadata();
    std::cout << "Title: " << metadata.title << std::endl;
    std::cout << "Author: " << metadata.GetPrimaryAuthor() << std::endl;
    std::cout << "Pages: " << engine->GetPageCount() << std::endl;
    
    // Render first page
    auto settings = UltraCanvas::EBookRenderSettings::Default();
    auto pageData = engine->RenderPage(1, settings);
    
    // Get table of contents
    auto toc = engine->GetTableOfContents();
    for (const auto& entry : toc) {
        std::cout << entry.title << " - Page " << entry.pageNumber << std::endl;
    }
}
```
*/
