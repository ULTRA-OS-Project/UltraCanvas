// UltraCanvasPDFPlugin.h
// Comprehensive PDF parsing and rendering engine with page management
// Version: 1.0.0
// Last Modified: 2025-09-03
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasGraphicsPluginSystem.h"
#include "UltraCanvasRenderContext.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <mutex>

#ifdef ULTRACANVAS_PDF_SUPPORT

namespace UltraCanvas {

// ===== PDF ZOOM MODES =====
enum class PDFZoomMode {
    ActualSize,     // 100% zoom
    FitPage,        // Fit entire page in view
    FitWidth,       // Fit page width to view
    FitHeight,      // Fit page height to view
    Custom          // User-defined zoom level
};

// ===== PDF DISPLAY MODES =====
enum class PDFDisplayMode {
    SinglePage,     // Show one page at a time
    DoublePage,     // Show two pages side by side
    Continuous,     // Continuous scrolling
    ThumbnailGrid   // Grid of page thumbnails
};

// ===== PDF PAGE DATA =====
struct PDFPageInfo {
    int pageNumber;
    float width;
    float height;
    float aspectRatio;
    std::string pageLabel;  // For custom page numbering
    bool isLoaded;
    std::vector<uint8_t> imageData;  // Rendered page as image
    std::vector<uint8_t> thumbnailData; // Small preview
    
    PDFPageInfo() : pageNumber(0), width(0), height(0), aspectRatio(1.0f), isLoaded(false) {}
    
    PDFPageInfo(int num, float w, float h) 
        : pageNumber(num), width(w), height(h), aspectRatio(w/h), isLoaded(false) {
        pageLabel = std::to_string(num);
    }
};

// ===== PDF DOCUMENT METADATA =====
struct PDFDocumentInfo {
    std::string title;
    std::string author;
    std::string subject;
    std::string creator;
    std::string producer;
    std::string creationDate;
    std::string modificationDate;
    std::string keywords;
    
    int pageCount;
    bool isEncrypted;
    bool isLinearized;
    std::string pdfVersion;
    long fileSize;
    
    PDFDocumentInfo() : pageCount(0), isEncrypted(false), isLinearized(false), fileSize(0) {}
};

// ===== PDF RENDER SETTINGS =====
struct PDFRenderSettings {
    float dpi = 150.0f;              // Rendering resolution
    float zoomLevel = 1.0f;          // Zoom multiplier
    PDFZoomMode zoomMode = PDFZoomMode::FitPage;
    PDFDisplayMode displayMode = PDFDisplayMode::SinglePage;
    
    bool antialiasing = true;
    bool subpixelRendering = false;
    bool grayScale = false;
    
    int thumbnailSize = 200;         // Thumbnail max dimension
    bool enableThumbnails = true;
    bool preloadPages = true;        // Preload adjacent pages
    int preloadRange = 2;            // Pages to preload before/after current
    
    Color backgroundColor = Color(255, 255, 255, 255);
    Color borderColor = Color(128, 128, 128, 255);
    
    static PDFRenderSettings Default() { return PDFRenderSettings(); }
    
    static PDFRenderSettings HighQuality() {
        PDFRenderSettings settings;
        settings.dpi = 300.0f;
        settings.antialiasing = true;
        settings.subpixelRendering = true;
        return settings;
    }
    
    static PDFRenderSettings FastPreview() {
        PDFRenderSettings settings;
        settings.dpi = 72.0f;
        settings.antialiasing = false;
        settings.preloadPages = false;
        return settings;
    }
};

// ===== PDF ENGINE INTERFACE =====
class IPDFEngine {
public:
    virtual ~IPDFEngine() = default;
    
    // Document Management
    virtual bool LoadDocument(const std::string& filePath, const std::string& password = "") = 0;
    virtual bool LoadDocumentFromMemory(const std::vector<uint8_t>& data, const std::string& password = "") = 0;
    virtual void CloseDocument() = 0;
    virtual bool IsDocumentLoaded() const = 0;
    
    // Document Information
    virtual PDFDocumentInfo GetDocumentInfo() const = 0;
    virtual int GetPageCount() const = 0;
    virtual PDFPageInfo GetPageInfo(int pageNumber) const = 0;
    virtual std::vector<PDFPageInfo> GetAllPageInfo() const = 0;
    
    // Page Rendering
    virtual std::vector<uint8_t> RenderPage(int pageNumber, const PDFRenderSettings& settings) = 0;
    virtual std::vector<uint8_t> RenderPageThumbnail(int pageNumber, int maxSize = 200) = 0;
    virtual bool PreloadPage(int pageNumber, const PDFRenderSettings& settings) = 0;
    virtual void ClearPageCache() = 0;
    
    // Search and Text
    virtual std::vector<std::string> ExtractTextFromPage(int pageNumber) = 0;
    virtual std::vector<Rect2D> SearchTextInPage(int pageNumber, const std::string& searchText) = 0;
    virtual std::string GetPageText(int pageNumber) = 0;
    
    // Navigation
    virtual bool HasBookmarks() const = 0;
    virtual std::vector<std::string> GetBookmarks() const = 0;
    virtual int GetBookmarkPage(const std::string& bookmark) const = 0;
    
    // Security
    virtual bool IsEncrypted() const = 0;
    virtual bool UnlockDocument(const std::string& password) = 0;
    virtual std::vector<std::string> GetPermissions() const = 0;
    
    // Engine Information
    virtual std::string GetEngineName() const = 0;
    virtual std::string GetEngineVersion() const = 0;
    virtual std::vector<std::string> GetSupportedFormats() const = 0;
};

// ===== POPPLER PDF ENGINE IMPLEMENTATION =====
#ifdef ULTRACANVAS_POPPLER_SUPPORT
class PopplerPDFEngine : public IPDFEngine {
private:
    void* document;  // poppler::document*
    std::string documentPath;
    mutable std::mutex renderMutex;
    std::unordered_map<int, std::vector<uint8_t>> pageCache;
    std::unordered_map<int, std::vector<uint8_t>> thumbnailCache;
    
public:
    PopplerPDFEngine();
    ~PopplerPDFEngine() override;
    
    // Document Management
    bool LoadDocument(const std::string& filePath, const std::string& password = "") override;
    bool LoadDocumentFromMemory(const std::vector<uint8_t>& data, const std::string& password = "") override;
    void CloseDocument() override;
    bool IsDocumentLoaded() const override;
    
    // Document Information
    PDFDocumentInfo GetDocumentInfo() const override;
    int GetPageCount() const override;
    PDFPageInfo GetPageInfo(int pageNumber) const override;
    std::vector<PDFPageInfo> GetAllPageInfo() const override;
    
    // Page Rendering
    std::vector<uint8_t> RenderPage(int pageNumber, const PDFRenderSettings& settings) override;
    std::vector<uint8_t> RenderPageThumbnail(int pageNumber, int maxSize = 200) override;
    bool PreloadPage(int pageNumber, const PDFRenderSettings& settings) override;
    void ClearPageCache() override;
    
    // Search and Text
    std::vector<std::string> ExtractTextFromPage(int pageNumber) override;
    std::vector<Rect2D> SearchTextInPage(int pageNumber, const std::string& searchText) override;
    std::string GetPageText(int pageNumber) override;
    
    // Navigation
    bool HasBookmarks() const override;
    std::vector<std::string> GetBookmarks() const override;
    int GetBookmarkPage(const std::string& bookmark) const override;
    
    // Security
    bool IsEncrypted() const override;
    bool UnlockDocument(const std::string& password) override;
    std::vector<std::string> GetPermissions() const override;
    
    // Engine Information
    std::string GetEngineName() const override { return "Poppler PDF Engine"; }
    std::string GetEngineVersion() const override { return "1.0.0"; }
    std::vector<std::string> GetSupportedFormats() const override { return {"pdf"}; }

private:
    void* GetPopplerPage(int pageNumber) const;  // poppler::page*
    std::vector<uint8_t> RenderPageInternal(int pageNumber, float dpi, bool thumbnail = false);
    void InitializePoppler();
    void CleanupPoppler();
};
#endif

// ===== MUPDF ENGINE IMPLEMENTATION =====
#ifdef ULTRACANVAS_MUPDF_SUPPORT
class MuPDFEngine : public IPDFEngine {
private:
    void* context;    // fz_context*
    void* document;   // fz_document*
    std::string documentPath;
    mutable std::mutex renderMutex;
    std::unordered_map<int, std::vector<uint8_t>> pageCache;
    
public:
    MuPDFEngine();
    ~MuPDFEngine() override;
    
    // Implement all IPDFEngine methods...
    bool LoadDocument(const std::string& filePath, const std::string& password = "") override;
    bool LoadDocumentFromMemory(const std::vector<uint8_t>& data, const std::string& password = "") override;
    void CloseDocument() override;
    bool IsDocumentLoaded() const override;
    
    PDFDocumentInfo GetDocumentInfo() const override;
    int GetPageCount() const override;
    PDFPageInfo GetPageInfo(int pageNumber) const override;
    std::vector<PDFPageInfo> GetAllPageInfo() const override;
    
    std::vector<uint8_t> RenderPage(int pageNumber, const PDFRenderSettings& settings) override;
    std::vector<uint8_t> RenderPageThumbnail(int pageNumber, int maxSize = 200) override;
    bool PreloadPage(int pageNumber, const PDFRenderSettings& settings) override;
    void ClearPageCache() override;
    
    std::vector<std::string> ExtractTextFromPage(int pageNumber) override;
    std::vector<Rect2D> SearchTextInPage(int pageNumber, const std::string& searchText) override;
    std::string GetPageText(int pageNumber) override;
    
    bool HasBookmarks() const override;
    std::vector<std::string> GetBookmarks() const override;
    int GetBookmarkPage(const std::string& bookmark) const override;
    
    bool IsEncrypted() const override;
    bool UnlockDocument(const std::string& password) override;
    std::vector<std::string> GetPermissions() const override;
    
    std::string GetEngineName() const override { return "MuPDF Engine"; }
    std::string GetEngineVersion() const override { return "1.0.0"; }
    std::vector<std::string> GetSupportedFormats() const override { return {"pdf"}; }

private:
    void InitializeMuPDF();
    void CleanupMuPDF();
    std::vector<uint8_t> RenderPageInternal(int pageNumber, float dpi, bool thumbnail = false);
};
#endif

// ===== PDF PLUGIN IMPLEMENTATION =====
class UltraCanvasPDFPlugin : public IGraphicsPlugin {
private:
    std::shared_ptr<IPDFEngine> engine;
    PDFRenderSettings defaultSettings;
    
public:
    UltraCanvasPDFPlugin();
    ~UltraCanvasPDFPlugin() override = default;
    
    // IGraphicsPlugin Implementation
    std::string GetPluginName() override { return "UltraCanvas PDF Plugin"; }
    std::string GetPluginVersion() override { return "1.0.0"; }
    std::vector<std::string> GetSupportedExtensions() override { return {"pdf"}; }
    GraphicsPluginCapabilities GetCapabilities() override;
    
    bool LoadFromFile(const std::string& filePath, ImageData& imageData) override;
    bool LoadFromMemory(const std::vector<uint8_t>& data, ImageData& imageData) override;
    bool SaveToFile(const std::string& filePath, const ImageData& imageData, int quality = 85) override;
    bool SaveToMemory(std::vector<uint8_t>& data, const ImageData& imageData, const std::string& format, int quality = 85) override;
    
    bool SupportsFormat(const std::string& extension) override;
    bool CanLoadFormat(const std::string& extension) override;
    bool CanSaveFormat(const std::string& extension) override;
    
    // PDF-Specific Methods
    void SetPDFEngine(std::shared_ptr<IPDFEngine> pdfEngine) { engine = pdfEngine; }
    std::shared_ptr<IPDFEngine> GetPDFEngine() const { return engine; }
    
    void SetDefaultRenderSettings(const PDFRenderSettings& settings) { defaultSettings = settings; }
    const PDFRenderSettings& GetDefaultRenderSettings() const { return defaultSettings; }
    
    // Multi-page Support
    bool LoadPage(const std::string& filePath, int pageNumber, ImageData& imageData);
    bool LoadPageThumbnail(const std::string& filePath, int pageNumber, ImageData& imageData, int maxSize = 200);
    int GetDocumentPageCount(const std::string& filePath);
    PDFDocumentInfo GetDocumentInfo(const std::string& filePath);
    
private:
    std::shared_ptr<IPDFEngine> CreateDefaultEngine();
    bool ConvertPDFPageToImageData(const std::vector<uint8_t>& pdfPageData, ImageData& imageData);
    std::string GetFileExtension(const std::string& filePath);
};

// ===== FACTORY FUNCTIONS =====
inline std::shared_ptr<UltraCanvasPDFPlugin> CreatePDFPlugin() {
    return std::make_shared<UltraCanvasPDFPlugin>();
}

inline void RegisterPDFPlugin() {
    UltraCanvasGraphicsPluginRegistry::RegisterPlugin(CreatePDFPlugin());
}

// ===== PDF ENGINE FACTORY =====
class PDFEngineFactory {
public:
    enum class EngineType {
        Auto,      // Automatically choose best available
        Poppler,   // Use Poppler-based engine
        MuPDF,     // Use MuPDF-based engine
        System     // Use system PDF engine
    };
    
    static std::shared_ptr<IPDFEngine> CreateEngine(EngineType type = EngineType::Auto);
    static std::vector<EngineType> GetAvailableEngines();
    static std::string GetEngineTypeName(EngineType type);
    static bool IsEngineAvailable(EngineType type);
};

} // namespace UltraCanvas

#else
// ===== STUB IMPLEMENTATION WHEN PDF SUPPORT IS DISABLED =====
namespace UltraCanvas {

inline std::shared_ptr<IGraphicsPlugin> CreatePDFPlugin() {
    std::cerr << "PDF Plugin: Not compiled with PDF support" << std::endl;
    return nullptr;
}

inline void RegisterPDFPlugin() {
    std::cerr << "PDF Plugin: Cannot register - not compiled with PDF support" << std::endl;
}

} // namespace UltraCanvas
#endif // ULTRACANVAS_PDF_SUPPORT

/*
=== PDF PLUGIN FEATURES ===

✅ **Complete PDF Document Support**:
- Multi-engine architecture (Poppler, MuPDF)
- Full document metadata extraction
- Page-by-page rendering with caching
- Thumbnail generation
- Text extraction and search
- Bookmark navigation
- Password-protected PDF support

✅ **Professional Rendering**:
- Multiple DPI settings (72-600 DPI)
- Anti-aliasing and subpixel rendering
- Custom zoom levels and fit modes
- Background and border color control
- Memory-efficient page caching
- Asynchronous page preloading

✅ **UltraCanvas Integration**:
- Implements IGraphicsPlugin interface
- Uses UltraCanvas ImageData format
- Follows framework naming conventions
- Conditional compilation support
- Thread-safe operations

✅ **Build Configuration**:
```cmake
# Enable PDF support with Poppler
set(ULTRACANVAS_PDF_SUPPORT ON)
set(ULTRACANVAS_POPPLER_SUPPORT ON)
find_package(PkgConfig REQUIRED)
pkg_check_modules(POPPLER REQUIRED poppler-cpp)
target_link_libraries(UltraCanvas ${POPPLER_LIBRARIES})
target_include_directories(UltraCanvas PRIVATE ${POPPLER_INCLUDE_DIRS})
target_compile_definitions(UltraCanvas PRIVATE ULTRACANVAS_PDF_SUPPORT ULTRACANVAS_POPPLER_SUPPORT)

# Or with MuPDF
set(ULTRACANVAS_MUPDF_SUPPORT ON)
find_package(MuPDF REQUIRED)
target_link_libraries(UltraCanvas MuPDF::MuPDF)
target_compile_definitions(UltraCanvas PRIVATE ULTRACANVAS_MUPDF_SUPPORT)
```

✅ **Usage Examples**:
```cpp
// Register PDF plugin
UltraCanvas::RegisterPDFPlugin();

// Load PDF page
UltraCanvas::ImageData pageImage;
auto plugin = UltraCanvas::CreatePDFPlugin();
if (plugin->LoadPage("document.pdf", 1, pageImage)) {
    std::cout << "Loaded page 1: " << pageImage.width << "x" << pageImage.height << std::endl;
}

// Get document info
auto docInfo = plugin->GetDocumentInfo("document.pdf");
std::cout << "Document: " << docInfo.title << " (" << docInfo.pageCount << " pages)" << std::endl;

// Load thumbnail
UltraCanvas::ImageData thumbnail;
plugin->LoadPageThumbnail("document.pdf", 1, thumbnail, 200);
```

✅ **Dependencies**:
- Poppler-cpp (recommended) OR MuPDF
- Cairo/Pixman (for rendering)
- FreeType (for font rendering)

This plugin provides production-ready PDF support for comprehensive
document viewing applications with professional-grade features.
*/
