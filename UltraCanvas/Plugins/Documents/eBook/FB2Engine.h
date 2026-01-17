// Plugins/Documents/eBook/FB2Engine.h
// FictionBook 2.0 (FB2) format engine implementation
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

// Conditional compilation for FB2 support
#ifdef ULTRACANVAS_FB2_SUPPORT

namespace UltraCanvas {

// ===== FB2 INTERNAL STRUCTURES =====

// FB2 Author Information
struct FB2Author {
    std::string firstName;
    std::string middleName;
    std::string lastName;
    std::string nickname;
    std::string homePage;
    std::string email;
    std::string id;
    
    std::string GetFullName() const {
        std::string name;
        if (!firstName.empty()) name += firstName;
        if (!middleName.empty()) {
            if (!name.empty()) name += " ";
            name += middleName;
        }
        if (!lastName.empty()) {
            if (!name.empty()) name += " ";
            name += lastName;
        }
        if (name.empty() && !nickname.empty()) {
            name = nickname;
        }
        return name;
    }
};

// FB2 Title Information
struct FB2TitleInfo {
    std::vector<std::string> genres;
    std::vector<FB2Author> authors;
    std::string bookTitle;
    std::string annotation;
    std::string keywords;
    std::string date;
    std::string coverPageImage;    // Image ID for cover
    std::string language;
    std::string srcLanguage;       // Original language if translated
    std::vector<FB2Author> translators;
    std::string sequenceName;
    int sequenceNumber = 0;
};

// FB2 Document Information
struct FB2DocumentInfo {
    std::vector<FB2Author> authors;
    std::string programUsed;
    std::string date;
    std::string srcUrl;
    std::string srcOcr;
    std::string id;
    std::string version;
    std::string history;
    std::vector<FB2Author> publishers;
};

// FB2 Publish Information
struct FB2PublishInfo {
    std::string bookName;
    std::string publisher;
    std::string city;
    std::string year;
    std::string isbn;
    std::string sequence;
    int sequenceNumber = 0;
};

// FB2 Custom Information
struct FB2CustomInfo {
    std::string infoType;
    std::string value;
};

// FB2 Binary Resource (embedded image)
struct FB2Binary {
    std::string id;
    std::string contentType;
    std::vector<uint8_t> data;
    int width = 0;
    int height = 0;
};

// FB2 Section (chapter or sub-section)
struct FB2Section {
    std::string id;
    std::string title;
    std::vector<std::string> epigraphs;
    std::string content;           // Raw XML content
    std::string plainText;         // Extracted plain text
    std::vector<FB2Section> sections;  // Nested sections
    std::vector<std::string> images;   // Image references
    int depth = 0;
    int startPage = 0;
    int pageCount = 0;
    
    int GetTotalSections() const {
        int count = 1;
        for (const auto& sub : sections) {
            count += sub.GetTotalSections();
        }
        return count;
    }
};

// FB2 Body (main content container)
struct FB2Body {
    std::string name;              // "main", "notes", "comments", etc.
    std::string title;
    std::vector<std::string> epigraphs;
    std::vector<FB2Section> sections;
    
    bool IsMainBody() const {
        return name.empty() || name == "main";
    }
    
    bool IsNotes() const {
        return name == "notes" || name == "footnotes";
    }
};

// FB2 Complete Document
struct FB2Document {
    // Description
    FB2TitleInfo titleInfo;
    FB2TitleInfo srcTitleInfo;     // Original title info if translated
    FB2DocumentInfo documentInfo;
    FB2PublishInfo publishInfo;
    std::vector<FB2CustomInfo> customInfo;
    
    // Content
    std::vector<FB2Body> bodies;
    
    // Resources
    std::unordered_map<std::string, FB2Binary> binaries;
    
    // Stylesheet
    std::string stylesheet;
    
    // Helper methods
    const FB2Body* GetMainBody() const {
        for (const auto& body : bodies) {
            if (body.IsMainBody()) return &body;
        }
        return bodies.empty() ? nullptr : &bodies[0];
    }
    
    const FB2Body* GetNotesBody() const {
        for (const auto& body : bodies) {
            if (body.IsNotes()) return &body;
        }
        return nullptr;
    }
    
    bool HasCover() const {
        return !titleInfo.coverPageImage.empty() && 
               binaries.find(titleInfo.coverPageImage) != binaries.end();
    }
};

// ===== FB2 ENGINE IMPLEMENTATION =====

class FB2Engine : public EBookEngineBase {
public:
    FB2Engine();
    virtual ~FB2Engine();
    
    // ===== IEBookEngine Implementation =====
    
    // Document lifecycle
    bool LoadDocument(const std::string& filePath, 
                      const std::string& password = "") override;
    bool LoadDocumentFromMemory(const std::vector<uint8_t>& data,
                                const std::string& password = "") override;
    void CloseDocument() override;
    
    // Format info
    EBookFormat GetFormat() const override { return EBookFormat::FB2; }
    std::string GetFormatVersion() const override { return "2.0"; }
    
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
    std::vector<std::string> GetEmbeddedFonts() const override { return {}; }
    std::vector<uint8_t> GetEmbeddedFont(const std::string&) const override { return {}; }
    std::vector<std::string> GetStylesheets() const override;
    std::string GetStylesheetContent(const std::string& stylesheetId) const override;
    
    // Engine info
    std::string GetEngineName() const override { return "UltraCanvas FB2 Engine"; }
    std::string GetEngineVersion() const override { return "1.0.0"; }
    std::vector<std::string> GetSupportedExtensions() const override {
        return {"fb2", "fb2.zip"};
    }
    EBookPluginCapabilities GetCapabilities() const override;
    
    // Validation
    bool ValidateFile(const std::string& filePath) const override;
    bool RequiresPassword(const std::string& filePath) const override { return false; }
    bool VerifyPassword(const std::string& password) const override { return true; }
    
    // ===== FB2-Specific Methods =====
    
    // Get the parsed FB2 document
    const FB2Document& GetFB2Document() const { return fb2Doc; }
    
    // Get section content
    std::string GetSectionContent(int sectionIndex) const;
    
    // Get notes/footnotes
    std::string GetNote(const std::string& noteId) const;
    
    // Convert FB2 markup to HTML for rendering
    std::string ConvertToHTML(const std::string& fb2Content) const;
    
private:
    // ===== Internal Data =====
    FB2Document fb2Doc;
    
    // Rendering
    mutable std::mutex renderMutex;
    EBookRenderSettings lastRenderSettings;
    
    // Page mapping
    struct PageMapping {
        int sectionIndex;
        int pageInSection;
        int globalPage;
        std::string sectionId;
    };
    std::vector<PageMapping> pageMappings;
    
    // Flattened sections for easy access
    std::vector<const FB2Section*> flattenedSections;
    
    // ===== Internal Methods =====
    
    // Parsing
    bool ParseFB2(const std::string& xmlContent);
    bool ParseDescription(void* descNode);
    bool ParseTitleInfo(void* titleInfoNode, FB2TitleInfo& info);
    bool ParseDocumentInfo(void* docInfoNode);
    bool ParsePublishInfo(void* pubInfoNode);
    bool ParseAuthor(void* authorNode, FB2Author& author);
    bool ParseBody(void* bodyNode, FB2Body& body);
    bool ParseSection(void* sectionNode, FB2Section& section, int depth);
    bool ParseBinary(void* binaryNode);
    
    // Content processing
    void ProcessContent();
    void FlattenSections();
    void FlattenSectionsRecursive(const std::vector<FB2Section>& sections);
    std::string ExtractPlainText(const std::string& xmlContent) const;
    std::string ExtractSectionPlainText(const FB2Section& section) const;
    
    // Page calculation
    void CalculatePages(const EBookRenderSettings& settings);
    int EstimatePageCount(const std::string& text,
                          const EBookRenderSettings& settings) const;
    
    // Rendering helpers
    std::vector<uint8_t> RenderSectionToImage(const FB2Section& section,
                                               int pageInSection,
                                               const EBookRenderSettings& settings);
    std::vector<uint8_t> RenderTextToImage(const std::string& text,
                                           const EBookRenderSettings& settings);
    
    // Metadata helpers
    void BuildMetadata();
    void BuildTableOfContents();
    void BuildTOCFromSections(const std::vector<FB2Section>& sections,
                              std::vector<EBookTOCEntry>& toc,
                              int& pageCounter,
                              int depth);
    void BuildPageInfos();
    
    // Image helpers
    bool DecodeBase64Image(const std::string& base64Data, 
                           std::vector<uint8_t>& outData) const;
    bool DecodeImage(const FB2Binary& binary,
                     std::vector<uint8_t>& outRGBA,
                     int& width, int& height) const;
    
    // Link resolution
    std::string ResolveImageRef(const std::string& href) const;
    int ResolveSectionRef(const std::string& href) const;
};

// ===== FACTORY FUNCTION =====

inline std::shared_ptr<IEBookEngine> CreateFB2Engine() {
    return std::make_shared<FB2Engine>();
}

// ===== FB2 PLUGIN CLASS =====

class UltraCanvasFB2Plugin {
public:
    UltraCanvasFB2Plugin() = default;
    ~UltraCanvasFB2Plugin() = default;
    
    std::string GetPluginName() const { return "UltraCanvas FB2 Plugin"; }
    std::string GetPluginVersion() const { return "1.0.0"; }
    std::vector<std::string> GetSupportedExtensions() const { return {"fb2", "fb2.zip"}; }
    
    bool CanHandle(const std::string& filePath) const;
    bool ValidateFile(const std::string& filePath) const;
    
    std::shared_ptr<FB2Engine> CreateEngine() const {
        return std::make_shared<FB2Engine>();
    }
    
    EBookPluginCapabilities GetCapabilities() const;
};

// ===== REGISTRATION FUNCTIONS =====

void RegisterFB2Plugin();
void UnregisterFB2Plugin();

} // namespace UltraCanvas

#else // ULTRACANVAS_FB2_SUPPORT not defined

namespace UltraCanvas {

// Stub implementation when FB2 support is disabled
class FB2Engine : public EBookEngineBase {
public:
    FB2Engine() {
        std::cerr << "FB2 Engine: Not compiled with FB2 support" << std::endl;
    }
    
    bool LoadDocument(const std::string&, const std::string& = "") override { return false; }
    bool LoadDocumentFromMemory(const std::vector<uint8_t>&, const std::string& = "") override { return false; }
    void CloseDocument() override {}
    
    EBookFormat GetFormat() const override { return EBookFormat::FB2; }
    std::string GetFormatVersion() const override { return ""; }
    
    std::vector<uint8_t> RenderPage(int, const EBookRenderSettings&) override { return {}; }
    std::vector<uint8_t> RenderPageThumbnail(int, int = 200) override { return {}; }
    Size2Di GetRenderedPageSize(int, const EBookRenderSettings&) const override { return {0, 0}; }
    bool PreloadPage(int, const EBookRenderSettings&) override { return false; }
    
    std::string ExtractPageText(int) const override { return ""; }
    std::string ExtractAllText() const override { return ""; }
    std::string GetTextAtPosition(int, float, float) const override { return ""; }
    std::string GetTextInRect(int, const Rect2Df&) const override { return ""; }
    
    std::vector<EBookSearchResult> SearchText(const std::string&, bool, bool) const override { return {}; }
    std::vector<EBookSearchResult> SearchTextInPage(int, const std::string&, bool) const override { return {}; }
    
    int GetChapterStartPage(int) const override { return 0; }
    int GetChapterForPage(int) const override { return -1; }
    int ResolveLink(const std::string&) const override { return 0; }
    
    bool SupportsReflow() const override { return false; }
    std::string GetReflowedContent(int, const EBookReflowSettings&) const override { return ""; }
    int CalculateReflowPages(const EBookReflowSettings&, const Size2Di&) const override { return 0; }
    
    bool HasCoverImage() const override { return false; }
    std::vector<uint8_t> GetCoverImage() const override { return {}; }
    Size2Di GetCoverImageSize() const override { return {0, 0}; }
    
    std::vector<std::string> GetEmbeddedImages() const override { return {}; }
    std::vector<uint8_t> GetEmbeddedImage(const std::string&) const override { return {}; }
    std::vector<std::string> GetEmbeddedFonts() const override { return {}; }
    std::vector<uint8_t> GetEmbeddedFont(const std::string&) const override { return {}; }
    std::vector<std::string> GetStylesheets() const override { return {}; }
    std::string GetStylesheetContent(const std::string&) const override { return ""; }
    
    std::string GetEngineName() const override { return "FB2 Engine (Disabled)"; }
    std::string GetEngineVersion() const override { return "0.0.0"; }
    std::vector<std::string> GetSupportedExtensions() const override { return {}; }
    EBookPluginCapabilities GetCapabilities() const override { return {}; }
    
    bool ValidateFile(const std::string&) const override { return false; }
    bool RequiresPassword(const std::string&) const override { return false; }
    bool VerifyPassword(const std::string&) const override { return false; }
};

inline std::shared_ptr<IEBookEngine> CreateFB2Engine() {
    std::cerr << "FB2 Engine: Cannot create - not compiled with FB2 support" << std::endl;
    return nullptr;
}

inline void RegisterFB2Plugin() {
    std::cerr << "FB2 Plugin: Cannot register - not compiled with FB2 support" << std::endl;
}

inline void UnregisterFB2Plugin() {}

} // namespace UltraCanvas

#endif // ULTRACANVAS_FB2_SUPPORT

/*
=== FB2 ENGINE FEATURES ===

✅ **Full FictionBook 2.0 Support**:
- Complete FB2 XML parsing
- Title-info, document-info, publish-info extraction
- Multiple author support
- Series and sequence handling
- Annotation and keywords

✅ **Content Structure**:
- Multiple body support (main, notes, footnotes)
- Nested section hierarchy
- Epigraph handling
- Poem and stanza support
- Cite and emphasis formatting

✅ **Resource Handling**:
- Embedded binary images (Base64 decoded)
- Cover image extraction
- Inline image references
- Stylesheet support

✅ **Navigation**:
- Automatic TOC generation from sections
- Section-based chapter navigation
- Footnote/note linking
- Internal reference resolution

✅ **Text Features**:
- Full text extraction
- Text search
- Text reflow with styling
- Plain text export

✅ **Build Configuration**:
```cmake
# Enable FB2 support
set(ULTRACANVAS_FB2_SUPPORT ON)

# Using pugixml for XML parsing
find_package(pugixml REQUIRED)
target_link_libraries(UltraCanvas pugixml::pugixml)

# For compressed FB2 files
find_package(ZLIB REQUIRED)
target_link_libraries(UltraCanvas ZLIB::ZLIB)

target_compile_definitions(UltraCanvas PRIVATE ULTRACANVAS_FB2_SUPPORT)
```

✅ **Dependencies**:
- pugixml or tinyxml2 (XML parsing)
- zlib (for .fb2.zip files)
- Base64 decoder (built-in)
- Image decoders (PNG, JPEG for covers)

✅ **Usage Example**:
```cpp
auto engine = UltraCanvas::CreateFB2Engine();

if (engine->LoadDocument("book.fb2")) {
    auto metadata = engine->GetMetadata();
    std::cout << "Title: " << metadata.title << std::endl;
    std::cout << "Author: " << metadata.GetPrimaryAuthor() << std::endl;
    
    // Get cover
    if (engine->HasCoverImage()) {
        auto cover = engine->GetCoverImage();
        // Display cover...
    }
    
    // Render page
    auto settings = UltraCanvas::EBookRenderSettings::Default();
    auto pageData = engine->RenderPage(1, settings);
}
```

✅ **FB2 Format Features**:
- Popular in Russia and Eastern Europe
- Self-contained XML format
- Embedded images in Base64
- Rich metadata support
- Simple, well-documented structure
*/
