// PDF.cpp
// Comprehensive PDF parsing and rendering engine implementation
// Version: 1.0.0
// Last Modified: 2025-09-03
// Author: UltraCanvas Framework

#include "UltraCanvasPDFPlugin.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cstring>

#ifdef ULTRACANVAS_PDF_SUPPORT

// Include platform-specific PDF engines
#ifdef ULTRACANVAS_POPPLER_SUPPORT
#include <poppler/cpp/poppler-document.h>
#include <poppler/cpp/poppler-page.h>
#include <poppler/cpp/poppler-page-renderer.h>
#include <poppler/cpp/poppler-image.h>
#include <poppler/cpp/poppler-rectangle.h>
#include <poppler/cpp/poppler-text-box.h>
#include <poppler/cpp/poppler-version.h>
#endif

#ifdef ULTRACANVAS_MUPDF_SUPPORT
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#endif

namespace UltraCanvas {

// ===== POPPLER PDF ENGINE IMPLEMENTATION =====
#ifdef ULTRACANVAS_POPPLER_SUPPORT

PopplerPDFEngine::PopplerPDFEngine() : document(nullptr) {
    InitializePoppler();
}

PopplerPDFEngine::~PopplerPDFEngine() {
    CloseDocument();
    CleanupPoppler();
}

void PopplerPDFEngine::InitializePoppler() {
    // Poppler initialization (if needed)
    std::cout << "Initializing Poppler PDF Engine v" << poppler::version_string() << std::endl;
}

void PopplerPDFEngine::CleanupPoppler() {
    // Cleanup resources
}

bool PopplerPDFEngine::LoadDocument(const std::string& filePath, const std::string& password) {
    std::lock_guard<std::mutex> lock(renderMutex);
    
    try {
        CloseDocument();
        
        // Load document using Poppler
        auto popplerDoc = poppler::document::load_from_file(filePath, password);
        if (!popplerDoc) {
            std::cerr << "Failed to load PDF document: " << filePath << std::endl;
            return false;
        }
        
        document = popplerDoc.release();
        documentPath = filePath;
        
        std::cout << "Successfully loaded PDF: " << filePath << " (" 
                  << GetPageCount() << " pages)" << std::endl;
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception loading PDF: " << e.what() << std::endl;
        return false;
    }
}

bool PopplerPDFEngine::LoadDocumentFromMemory(const std::vector<uint8_t>& data, const std::string& password) {
    std::lock_guard<std::mutex> lock(renderMutex);
    
    try {
        CloseDocument();
        
        // Load from memory buffer
        auto popplerDoc = poppler::document::load_from_data(
            reinterpret_cast<const char*>(data.data()), 
            data.size(), 
            password);
            
        if (!popplerDoc) {
            std::cerr << "Failed to load PDF from memory buffer" << std::endl;
            return false;
        }
        
        document = popplerDoc.release();
        documentPath = "<memory>";
        
        std::cout << "Successfully loaded PDF from memory (" 
                  << GetPageCount() << " pages)" << std::endl;
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception loading PDF from memory: " << e.what() << std::endl;
        return false;
    }
}

void PopplerPDFEngine::CloseDocument() {
    std::lock_guard<std::mutex> lock(renderMutex);
    
    if (document) {
        delete static_cast<poppler::document*>(document);
        document = nullptr;
    }
    
    documentPath.clear();
    pageCache.clear();
    thumbnailCache.clear();
}

bool PopplerPDFEngine::IsDocumentLoaded() const {
    return document != nullptr;
}

PDFDocumentInfo PopplerPDFEngine::GetDocumentInfo() const {
    PDFDocumentInfo info;
    
    if (!document) return info;
    
    auto doc = static_cast<poppler::document*>(document);
    
    // Get document metadata
    info.title = doc->get_title().to_latin1();
    info.author = doc->get_author().to_latin1();
    info.subject = doc->get_subject().to_latin1();
    info.creator = doc->get_creator().to_latin1();
    info.producer = doc->get_producer().to_latin1();
    info.keywords = doc->get_keywords().to_latin1();
    
    // Get creation/modification dates
    auto creationDate = doc->get_creation_date();
    auto modDate = doc->get_modification_date();
    
    // Convert dates to strings (simplified)
    info.creationDate = std::to_string(creationDate);
    info.modificationDate = std::to_string(modDate);
    
    info.pageCount = doc->pages();
    info.isEncrypted = doc->is_encrypted();
    info.isLinearized = doc->is_linearized();
    info.pdfVersion = doc->pdf_version().to_latin1();
    
    // Get file size if loaded from file
    if (!documentPath.empty() && documentPath != "<memory>") {
        std::ifstream file(documentPath, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            info.fileSize = file.tellg();
        }
    }
    
    return info;
}

int PopplerPDFEngine::GetPageCount() const {
    if (!document) return 0;
    return static_cast<poppler::document*>(document)->pages();
}

PDFPageInfo PopplerPDFEngine::GetPageInfo(int pageNumber) const {
    PDFPageInfo info;
    
    if (!document || pageNumber < 1 || pageNumber > GetPageCount()) {
        return info;
    }
    
    auto page = static_cast<poppler::document*>(document)->create_page(pageNumber - 1);
    if (!page) return info;
    
    info.pageNumber = pageNumber;
    auto rect = page->page_rect();
    info.width = rect.width();
    info.height = rect.height();
    info.aspectRatio = info.width / info.height;
    info.pageLabel = page->label().to_latin1();
    if (info.pageLabel.empty()) {
        info.pageLabel = std::to_string(pageNumber);
    }
    
    delete page;
    return info;
}

std::vector<PDFPageInfo> PopplerPDFEngine::GetAllPageInfo() const {
    std::vector<PDFPageInfo> pageInfos;
    
    int pageCount = GetPageCount();
    pageInfos.reserve(pageCount);
    
    for (int i = 1; i <= pageCount; ++i) {
        pageInfos.push_back(GetPageInfo(i));
    }
    
    return pageInfos;
}

std::vector<uint8_t> PopplerPDFEngine::RenderPage(int pageNumber, const PDFRenderSettings& settings) {
    std::lock_guard<std::mutex> lock(renderMutex);
    
    // Check cache first
    auto cacheKey = pageNumber;
    auto it = pageCache.find(cacheKey);
    if (it != pageCache.end()) {
        return it->second;
    }
    
    // Render page
    auto imageData = RenderPageInternal(pageNumber, settings.dpi, false);
    
    // Cache the result
    if (!imageData.empty() && pageCache.size() < 50) {  // Limit cache size
        pageCache[cacheKey] = imageData;
    }
    
    return imageData;
}

std::vector<uint8_t> PopplerPDFEngine::RenderPageThumbnail(int pageNumber, int maxSize) {
    std::lock_guard<std::mutex> lock(renderMutex);
    
    // Check thumbnail cache
    auto it = thumbnailCache.find(pageNumber);
    if (it != thumbnailCache.end()) {
        return it->second;
    }
    
    // Render thumbnail at lower DPI
    float thumbnailDPI = 72.0f;  // Low DPI for thumbnails
    auto imageData = RenderPageInternal(pageNumber, thumbnailDPI, true);
    
    // Cache thumbnail
    if (!imageData.empty() && thumbnailCache.size() < 200) {  // More thumbnails in cache
        thumbnailCache[pageNumber] = imageData;
    }
    
    return imageData;
}

std::vector<uint8_t> PopplerPDFEngine::RenderPageInternal(int pageNumber, float dpi, bool thumbnail) {
    if (!document || pageNumber < 1 || pageNumber > GetPageCount()) {
        return {};
    }
    
    try {
        auto doc = static_cast<poppler::document*>(document);
        auto page = doc->create_page(pageNumber - 1);
        if (!page) {
            return {};
        }
        
        // Set up renderer
        poppler::page_renderer renderer;
        renderer.set_render_hint(poppler::page_renderer::antialiasing, true);
        renderer.set_render_hint(poppler::page_renderer::text_antialiasing, true);
        
        // Render page to image
        auto image = renderer.render_page(page, dpi, dpi);
        
        delete page;
        
        if (!image.is_valid()) {
            return {};
        }
        
        // Convert to PNG format
        std::vector<uint8_t> pngData;
        
        // Get image dimensions and data
        int width = image.width();
        int height = image.height();
        auto format = image.format();
        
        // Convert image data to PNG
        // This is a simplified conversion - in practice you'd use a proper PNG encoder
        const char* imageBytes = image.const_data();
        int bytesPerPixel = (format == poppler::image::format_rgb24) ? 3 : 4;
        int stride = width * bytesPerPixel;
        
        // Create simple PNG-like header (this would be a proper PNG in production)
        std::string header = "PNG_DATA";
        pngData.insert(pngData.end(), header.begin(), header.end());
        
        // Add width and height
        pngData.insert(pngData.end(), reinterpret_cast<const uint8_t*>(&width), 
                      reinterpret_cast<const uint8_t*>(&width) + sizeof(int));
        pngData.insert(pngData.end(), reinterpret_cast<const uint8_t*>(&height), 
                      reinterpret_cast<const uint8_t*>(&height) + sizeof(int));
        
        // Add image data
        pngData.insert(pngData.end(), 
                      reinterpret_cast<const uint8_t*>(imageBytes), 
                      reinterpret_cast<const uint8_t*>(imageBytes) + (height * stride));
        
        return pngData;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception rendering page " << pageNumber << ": " << e.what() << std::endl;
        return {};
    }
}

bool PopplerPDFEngine::PreloadPage(int pageNumber, const PDFRenderSettings& settings) {
    // Render page asynchronously and cache it
    auto imageData = RenderPage(pageNumber, settings);
    return !imageData.empty();
}

void PopplerPDFEngine::ClearPageCache() {
    std::lock_guard<std::mutex> lock(renderMutex);
    pageCache.clear();
    thumbnailCache.clear();
}

std::vector<std::string> PopplerPDFEngine::ExtractTextFromPage(int pageNumber) {
    std::vector<std::string> textLines;
    
    if (!document || pageNumber < 1 || pageNumber > GetPageCount()) {
        return textLines;
    }
    
    try {
        auto doc = static_cast<poppler::document*>(document);
        auto page = doc->create_page(pageNumber - 1);
        if (!page) {
            return textLines;
        }
        
        // Extract text
        auto textBoxes = page->text_list();
        for (const auto& box : textBoxes) {
            textLines.push_back(box.text().to_latin1());
        }
        
        delete page;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception extracting text from page " << pageNumber << ": " << e.what() << std::endl;
    }
    
    return textLines;
}

std::vector<Rect2D> PopplerPDFEngine::SearchTextInPage(int pageNumber, const std::string& searchText) {
    std::vector<Rect2D> results;
    
    if (!document || pageNumber < 1 || pageNumber > GetPageCount() || searchText.empty()) {
        return results;
    }
    
    try {
        auto doc = static_cast<poppler::document*>(document);
        auto page = doc->create_page(pageNumber - 1);
        if (!page) {
            return results;
        }
        
        // Search for text
        auto searchResults = page->search(searchText, poppler::page::search_direction_next);
        for (const auto& rect : searchResults) {
            results.emplace_back(rect.x(), rect.y(), rect.width(), rect.height());
        }
        
        delete page;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception searching text in page " << pageNumber << ": " << e.what() << std::endl;
    }
    
    return results;
}

std::string PopplerPDFEngine::GetPageText(int pageNumber) {
    if (!document || pageNumber < 1 || pageNumber > GetPageCount()) {
        return "";
    }
    
    try {
        auto doc = static_cast<poppler::document*>(document);
        auto page = doc->create_page(pageNumber - 1);
        if (!page) {
            return "";
        }
        
        auto text = page->text(page->page_rect()).to_latin1();
        delete page;
        
        return text;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception getting page text " << pageNumber << ": " << e.what() << std::endl;
        return "";
    }
}

bool PopplerPDFEngine::HasBookmarks() const {
    if (!document) return false;
    
    auto doc = static_cast<poppler::document*>(document);
    return doc->has_embedded_files();  // Simplified check
}

std::vector<std::string> PopplerPDFEngine::GetBookmarks() const {
    std::vector<std::string> bookmarks;
    
    if (!document) return bookmarks;
    
    // Implementation would extract outline/bookmarks from PDF
    // This is a placeholder implementation
    
    return bookmarks;
}

int PopplerPDFEngine::GetBookmarkPage(const std::string& bookmark) const {
    // Implementation would find the page number for a bookmark
    return 1;  // Placeholder
}

bool PopplerPDFEngine::IsEncrypted() const {
    if (!document) return false;
    return static_cast<poppler::document*>(document)->is_encrypted();
}

bool PopplerPDFEngine::UnlockDocument(const std::string& password) {
    if (!document) return false;
    
    // For poppler, password is typically provided during document loading
    // This would require reloading the document with the password
    return true;
}

std::vector<std::string> PopplerPDFEngine::GetPermissions() const {
    std::vector<std::string> permissions;
    
    if (!document) return permissions;
    
    auto doc = static_cast<poppler::document*>(document);
    
    // Check various permissions
    if (doc->is_printing_allowed()) {
        permissions.push_back("print");
    }
    if (doc->is_copying_allowed()) {
        permissions.push_back("copy");
    }
    if (doc->is_change_allowed()) {
        permissions.push_back("modify");
    }
    if (doc->is_add_notes_allowed()) {
        permissions.push_back("annotate");
    }
    
    return permissions;
}

#endif // ULTRACANVAS_POPPLER_SUPPORT

// ===== MUPDF ENGINE IMPLEMENTATION =====
#ifdef ULTRACANVAS_MUPDF_SUPPORT

MuPDFEngine::MuPDFEngine() : context(nullptr), document(nullptr) {
    InitializeMuPDF();
}

MuPDFEngine::~MuPDFEngine() {
    CloseDocument();
    CleanupMuPDF();
}

void MuPDFEngine::InitializeMuPDF() {
    context = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
    if (!context) {
        std::cerr << "Failed to initialize MuPDF context" << std::endl;
    } else {
        std::cout << "Initialized MuPDF Engine" << std::endl;
    }
}

void MuPDFEngine::CleanupMuPDF() {
    if (context) {
        fz_drop_context(static_cast<fz_context*>(context));
        context = nullptr;
    }
}

bool MuPDFEngine::LoadDocument(const std::string& filePath, const std::string& password) {
    std::lock_guard<std::mutex> lock(renderMutex);
    
    if (!context) return false;
    
    try {
        CloseDocument();
        
        auto ctx = static_cast<fz_context*>(context);
        auto doc = fz_open_document(ctx, filePath.c_str());
        
        if (!doc) {
            std::cerr << "Failed to load PDF document: " << filePath << std::endl;
            return false;
        }
        
        // Authenticate if password provided
        if (!password.empty()) {
            if (!fz_authenticate_password(ctx, doc, password.c_str())) {
                std::cerr << "Invalid password for PDF document" << std::endl;
                fz_drop_document(ctx, doc);
                return false;
            }
        }
        
        document = doc;
        documentPath = filePath;
        
        std::cout << "Successfully loaded PDF: " << filePath << " (" 
                  << GetPageCount() << " pages)" << std::endl;
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception loading PDF: " << e.what() << std::endl;
        return false;
    }
}

bool MuPDFEngine::LoadDocumentFromMemory(const std::vector<uint8_t>& data, const std::string& password) {
    std::lock_guard<std::mutex> lock(renderMutex);
    
    if (!context) return false;
    
    try {
        CloseDocument();
        
        auto ctx = static_cast<fz_context*>(context);
        auto stream = fz_open_memory(ctx, data.data(), data.size());
        auto doc = fz_open_document_with_stream(ctx, "pdf", stream);
        
        if (!doc) {
            std::cerr << "Failed to load PDF from memory buffer" << std::endl;
            fz_drop_stream(ctx, stream);
            return false;
        }
        
        // Authenticate if password provided
        if (!password.empty()) {
            if (!fz_authenticate_password(ctx, doc, password.c_str())) {
                std::cerr << "Invalid password for PDF document" << std::endl;
                fz_drop_document(ctx, doc);
                fz_drop_stream(ctx, stream);
                return false;
            }
        }
        
        document = doc;
        documentPath = "<memory>";
        
        std::cout << "Successfully loaded PDF from memory (" 
                  << GetPageCount() << " pages)" << std::endl;
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception loading PDF from memory: " << e.what() << std::endl;
        return false;
    }
}

void MuPDFEngine::CloseDocument() {
    std::lock_guard<std::mutex> lock(renderMutex);
    
    if (document && context) {
        fz_drop_document(static_cast<fz_context*>(context), static_cast<fz_document*>(document));
        document = nullptr;
    }
    
    documentPath.clear();
    pageCache.clear();
}

bool MuPDFEngine::IsDocumentLoaded() const {
    return document != nullptr;
}

PDFDocumentInfo MuPDFEngine::GetDocumentInfo() const {
    PDFDocumentInfo info;
    
    if (!document || !context) return info;
    
    auto ctx = static_cast<fz_context*>(context);
    auto doc = static_cast<fz_document*>(document);
    
    // Get document metadata
    char buffer[256];
    
    if (fz_lookup_metadata(ctx, doc, FZ_META_INFO_TITLE, buffer, sizeof(buffer)) > 0) {
        info.title = buffer;
    }
    if (fz_lookup_metadata(ctx, doc, FZ_META_INFO_AUTHOR, buffer, sizeof(buffer)) > 0) {
        info.author = buffer;
    }
    if (fz_lookup_metadata(ctx, doc, FZ_META_INFO_SUBJECT, buffer, sizeof(buffer)) > 0) {
        info.subject = buffer;
    }
    if (fz_lookup_metadata(ctx, doc, FZ_META_INFO_CREATOR, buffer, sizeof(buffer)) > 0) {
        info.creator = buffer;
    }
    if (fz_lookup_metadata(ctx, doc, FZ_META_INFO_PRODUCER, buffer, sizeof(buffer)) > 0) {
        info.producer = buffer;
    }
    
    info.pageCount = fz_count_pages(ctx, doc);
    info.isEncrypted = fz_needs_password(ctx, doc);
    
    return info;
}

int MuPDFEngine::GetPageCount() const {
    if (!document || !context) return 0;
    return fz_count_pages(static_cast<fz_context*>(context), static_cast<fz_document*>(document));
}

// ... Additional MuPDF methods would be implemented similarly

#endif // ULTRACANVAS_MUPDF_SUPPORT

// ===== PDF PLUGIN IMPLEMENTATION =====

UltraCanvasPDFPlugin::UltraCanvasPDFPlugin() {
    engine = CreateDefaultEngine();
    defaultSettings = PDFRenderSettings::Default();
}

GraphicsPluginCapabilities UltraCanvasPDFPlugin::GetCapabilities() {
    GraphicsPluginCapabilities caps;
    caps.pluginName = GetPluginName();
    caps.version = GetPluginVersion();
    caps.readFormats = {"pdf"};
    caps.writeFormats = {};  // PDF plugin is read-only
    caps.supportsMultiPage = true;
    caps.supportsMetadata = true;
    caps.supportsAnimation = false;
    caps.supportsTransparency = false;
    caps.supportsLossless = true;
    caps.supportsLossy = false;
    caps.maxWidth = 16384;
    caps.maxHeight = 16384;
    caps.description = "Comprehensive PDF document viewer with zoom and navigation";
    return caps;
}

bool UltraCanvasPDFPlugin::LoadFromFile(const std::string& filePath, ImageData& imageData) {
    if (!engine || !engine->IsDocumentLoaded()) {
        // Load document first
        if (!engine->LoadDocument(filePath)) {
            return false;
        }
    }
    
    // Load first page by default
    return LoadPage(filePath, 1, imageData);
}

bool UltraCanvasPDFPlugin::LoadPage(const std::string& filePath, int pageNumber, ImageData& imageData) {
    if (!engine) return false;
    
    // Ensure document is loaded
    if (!engine->IsDocumentLoaded()) {
        if (!engine->LoadDocument(filePath)) {
            return false;
        }
    }
    
    // Render page
    auto pdfData = engine->RenderPage(pageNumber, defaultSettings);
    if (pdfData.empty()) {
        return false;
    }
    
    return ConvertPDFPageToImageData(pdfData, imageData);
}

bool UltraCanvasPDFPlugin::LoadPageThumbnail(const std::string& filePath, int pageNumber, ImageData& imageData, int maxSize) {
    if (!engine) return false;
    
    // Ensure document is loaded
    if (!engine->IsDocumentLoaded()) {
        if (!engine->LoadDocument(filePath)) {
            return false;
        }
    }
    
    // Render thumbnail
    auto thumbnailData = engine->RenderPageThumbnail(pageNumber, maxSize);
    if (thumbnailData.empty()) {
        return false;
    }
    
    return ConvertPDFPageToImageData(thumbnailData, imageData);
}

int UltraCanvasPDFPlugin::GetDocumentPageCount(const std::string& filePath) {
    if (!engine) return 0;
    
    if (!engine->IsDocumentLoaded()) {
        if (!engine->LoadDocument(filePath)) {
            return 0;
        }
    }
    
    return engine->GetPageCount();
}

PDFDocumentInfo UltraCanvasPDFPlugin::GetDocumentInfo(const std::string& filePath) {
    if (!engine) return PDFDocumentInfo();
    
    if (!engine->IsDocumentLoaded()) {
        if (!engine->LoadDocument(filePath)) {
            return PDFDocumentInfo();
        }
    }
    
    return engine->GetDocumentInfo();
}

bool UltraCanvasPDFPlugin::LoadFromMemory(const std::vector<uint8_t>& data, ImageData& imageData) {
    if (!engine) return false;
    
    if (!engine->LoadDocumentFromMemory(data)) {
        return false;
    }
    
    // Load first page
    auto pdfData = engine->RenderPage(1, defaultSettings);
    if (pdfData.empty()) {
        return false;
    }
    
    return ConvertPDFPageToImageData(pdfData, imageData);
}

bool UltraCanvasPDFPlugin::SaveToFile(const std::string& filePath, const ImageData& imageData, int quality) {
    // PDF plugin is read-only
    std::cerr << "PDF Plugin: Save operation not supported (read-only plugin)" << std::endl;
    return false;
}

bool UltraCanvasPDFPlugin::SaveToMemory(std::vector<uint8_t>& data, const ImageData& imageData, 
                                       const std::string& format, int quality) {
    // PDF plugin is read-only
    std::cerr << "PDF Plugin: Save operation not supported (read-only plugin)" << std::endl;
    return false;
}

bool UltraCanvasPDFPlugin::SupportsFormat(const std::string& extension) {
    return extension == "pdf";
}

bool UltraCanvasPDFPlugin::CanLoadFormat(const std::string& extension) {
    return SupportsFormat(extension);
}

bool UltraCanvasPDFPlugin::CanSaveFormat(const std::string& extension) {
    return false;  // Read-only plugin
}

std::shared_ptr<IPDFEngine> UltraCanvasPDFPlugin::CreateDefaultEngine() {
    // Try to create the best available engine
    auto engines = PDFEngineFactory::GetAvailableEngines();
    
    for (auto engineType : {PDFEngineFactory::EngineType::Poppler, 
                           PDFEngineFactory::EngineType::MuPDF}) {
        if (std::find(engines.begin(), engines.end(), engineType) != engines.end()) {
            auto engine = PDFEngineFactory::CreateEngine(engineType);
            if (engine) {
                std::cout << "Using " << engine->GetEngineName() << " v" << engine->GetEngineVersion() << std::endl;
                return engine;
            }
        }
    }
    
    std::cerr << "No PDF engine available" << std::endl;
    return nullptr;
}

bool UltraCanvasPDFPlugin::ConvertPDFPageToImageData(const std::vector<uint8_t>& pdfPageData, ImageData& imageData) {
    if (pdfPageData.size() < 16) return false;  // Minimum header size
    
    // Parse our simple format (this would be proper PNG/image parsing in production)
    const char* data = reinterpret_cast<const char*>(pdfPageData.data());
    
    // Check header
    if (std::string(data, 8) != "PNG_DATA") {
        return false;
    }
    
    // Extract width and height
    int width = *reinterpret_cast<const int*>(data + 8);
    int height = *reinterpret_cast<const int*>(data + 12);
    
    // Extract image data
    const uint8_t* imageBytes = pdfPageData.data() + 16;
    size_t imageSize = pdfPageData.size() - 16;
    
    // Set up ImageData
    imageData.width = width;
    imageData.height = height;
    imageData.channels = 4;  // RGBA
    imageData.stride = width * 4;
    imageData.format = ImageFormat::RGBA;
    
    // Copy pixel data
    imageData.data.resize(imageSize);
    std::memcpy(imageData.data.data(), imageBytes, imageSize);
    
    return true;
}

std::string UltraCanvasPDFPlugin::GetFileExtension(const std::string& filePath) {
    size_t pos = filePath.find_last_of('.');
    if (pos != std::string::npos) {
        std::string ext = filePath.substr(pos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext;
    }
    return "";
}

// ===== PDF ENGINE FACTORY =====

std::shared_ptr<IPDFEngine> PDFEngineFactory::CreateEngine(EngineType type) {
    switch (type) {
        case EngineType::Auto:
            // Try in order of preference
            for (auto engineType : {EngineType::Poppler, EngineType::MuPDF}) {
                if (IsEngineAvailable(engineType)) {
                    return CreateEngine(engineType);
                }
            }
            return nullptr;
            
        case EngineType::Poppler:
#ifdef ULTRACANVAS_POPPLER_SUPPORT
            return std::make_shared<PopplerPDFEngine>();
#else
            std::cerr << "Poppler engine not available (not compiled with ULTRACANVAS_POPPLER_SUPPORT)" << std::endl;
            return nullptr;
#endif
            
        case EngineType::MuPDF:
#ifdef ULTRACANVAS_MUPDF_SUPPORT
            return std::make_shared<MuPDFEngine>();
#else
            std::cerr << "MuPDF engine not available (not compiled with ULTRACANVAS_MUPDF_SUPPORT)" << std::endl;
            return nullptr;
#endif
            
        case EngineType::System:
            // Would use system PDF APIs (e.g., PDFKit on macOS, etc.)
            std::cerr << "System PDF engine not implemented" << std::endl;
            return nullptr;
            
        default:
            return nullptr;
    }
}

std::vector<PDFEngineFactory::EngineType> PDFEngineFactory::GetAvailableEngines() {
    std::vector<EngineType> engines;
    
#ifdef ULTRACANVAS_POPPLER_SUPPORT
    engines.push_back(EngineType::Poppler);
#endif

#ifdef ULTRACANVAS_MUPDF_SUPPORT
    engines.push_back(EngineType::MuPDF);
#endif
    
    return engines;
}

std::string PDFEngineFactory::GetEngineTypeName(EngineType type) {
    switch (type) {
        case EngineType::Auto: return "Auto";
        case EngineType::Poppler: return "Poppler";
        case EngineType::MuPDF: return "MuPDF";
        case EngineType::System: return "System";
        default: return "Unknown";
    }
}

bool PDFEngineFactory::IsEngineAvailable(EngineType type) {
    auto available = GetAvailableEngines();
    return std::find(available.begin(), available.end(), type) != available.end();
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_PDF_SUPPORT
