// UltraCanvasDocument.h
// UltraCanvas Document format for saving/loading windows as templates or interactive documents
// Version: 1.0.0
// Last Modified: 2025-08-26
// Author: UltraCanvas Framework
#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <variant>
#include <functional>

namespace UltraCanvas {

// Forward declarations
class UltraCanvasWindowBase;
class UltraCanvasComponent;
class UltraCanvasPage;

// Document file format version
constexpr const char* UCD_FORMAT_VERSION = "1.0.0";
constexpr const char* UCD_FILE_EXTENSION = ".ucd";

// Document file types
enum class UCDocumentType {
    Template,       // IDE template file
    Interactive,    // Interactive document with form elements
    Mixed          // Both template and interactive elements
};

// Compression types
enum class UCCompressionType {
    None,
    ZIP,
    GZIP,
    LZMA
};

// Encryption types
enum class UCEncryptionType {
    None,
    AES256,
    ChaCha20
};

// Document metadata
struct UCDocumentMetadata {
    std::string Title;
    std::string Author;
    std::string Description;
    std::string CreatedDate;
    std::string ModifiedDate;
    std::string Version;
    UCDocumentType DocumentType;
    std::map<std::string, std::string> CustomProperties;
};

// Page structure for multi-page documents
struct UCPageData {
    std::string PageId;
    std::string PageName;
    int PageNumber;
    int Width;
    int Height;
    std::string BackgroundColor;
    std::string BackgroundImage;
    std::map<std::string, std::variant<std::string, int, double, bool>> PageProperties;
    std::vector<std::shared_ptr<UCComponentData>> Components;
    
    // Navigation and linking
    std::string NextPageId;
    std::string PreviousPageId;
    std::vector<std::string> LinkedPages; // For hyperlinks and cross-references
    
    // Page-specific media resources
    std::vector<std::string> MediaResourceIds;
    
    // Page layout settings
    struct {
        int MarginTop = 0;
        int MarginBottom = 0;
        int MarginLeft = 0;
        int MarginRight = 0;
        bool ShowHeader = false;
        bool ShowFooter = false;
        bool ShowPageNumbers = false;
        std::string HeaderText;
        std::string FooterText;
    } LayoutSettings;
};

// Document navigation structure
struct UCDocumentNavigation {
    std::vector<std::string> PageOrder; // Ordered list of page IDs
    std::string CurrentPageId;
    std::map<std::string, std::string> Bookmarks; // Name -> PageId
    std::vector<std::pair<std::string, std::string>> TableOfContents; // Title -> PageId
    
    // Navigation history for interactive documents
    std::vector<std::string> NavigationHistory;
    int HistoryPosition = -1;
};

// Page transition and animation settings
enum class UCPageTransition {
    None,
    Fade,
    Slide,
    Flip,
    Zoom,
    Dissolve
};

struct UCPageTransitionSettings {
    UCPageTransition TransitionType = UCPageTransition::None;
    int DurationMs = 300;
    std::string EasingFunction = "ease-in-out";
    bool AnimateOnLoad = false;
};

// Component data structure for serialization
struct UCComponentData {
    std::string ComponentType;
    std::string ComponentId;
    std::map<std::string, std::variant<std::string, int, double, bool>> Properties;
    std::vector<std::shared_ptr<UCComponentData>> Children;
    
    // For interactive elements
    std::string EventHandlers;  // JavaScript-like event handling
    std::map<std::string, std::string> ValidationRules;
};

// Window layout data (now supports multiple pages)
struct UCWindowData {
    std::string WindowId;
    std::string Title;
    int Width;
    int Height;
    int PositionX;
    int PositionY;
    std::map<std::string, std::variant<std::string, int, double, bool>> WindowProperties;
    
    // Multi-page support
    std::vector<std::shared_ptr<UCPageData>> Pages;
    UCDocumentNavigation Navigation;
    UCPageTransitionSettings PageTransitions;
    
    // Legacy single-page support (for backwards compatibility)
    std::vector<std::shared_ptr<UCComponentData>> Components; // Deprecated: use Pages instead
};

// Media resource data
struct UCMediaResource {
    std::string ResourceId;
    std::string ResourceType; // image, audio, video, document, etc.
    std::string MimeType;
    std::string FileName;
    std::vector<uint8_t> Data;
    bool IsEmbedded;
    std::string ExternalPath; // If not embedded
};

// Document security settings
struct UCSecuritySettings {
    UCEncryptionType EncryptionType;
    std::string PasswordHash;
    std::string Salt;
    bool AllowPrint;
    bool AllowCopy;
    bool AllowEdit;
    bool AllowFormFilling;
};

// Main document structure
class UltraCanvasDocument {
public:
    // Constructor/Destructor
    UltraCanvasDocument();
    explicit UltraCanvasDocument(UCDocumentType documentType);
    ~UltraCanvasDocument();

    // Document creation and management
    bool CreateNewDocument(UCDocumentType documentType);
    bool LoadFromFile(const std::string& filePath, const std::string& password = "");
    bool SaveToFile(const std::string& filePath, UCCompressionType compression = UCCompressionType::ZIP, 
                   const std::string& password = "");
    
    // Window management
    bool AddWindow(const std::shared_ptr<UltraCanvasWindow>& window);
    bool RemoveWindow(const std::string& windowId);
    std::shared_ptr<UCWindowData> GetWindowData(const std::string& windowId);
    std::vector<std::string> GetWindowIds() const;
    
    // Page management
    bool AddPage(const std::string& windowId, const std::shared_ptr<UCPageData>& pageData);
    bool RemovePage(const std::string& windowId, const std::string& pageId);
    bool InsertPage(const std::string& windowId, int position, const std::shared_ptr<UCPageData>& pageData);
    bool MovePage(const std::string& windowId, const std::string& pageId, int newPosition);
    std::shared_ptr<UCPageData> GetPageData(const std::string& windowId, const std::string& pageId);
    std::shared_ptr<UCPageData> GetPageByNumber(const std::string& windowId, int pageNumber);
    std::vector<std::string> GetPageIds(const std::string& windowId) const;
    int GetPageCount(const std::string& windowId) const;
    
    // Page navigation
    bool NavigateToPage(const std::string& windowId, const std::string& pageId);
    bool NavigateToPageNumber(const std::string& windowId, int pageNumber);
    bool NavigateNext(const std::string& windowId);
    bool NavigatePrevious(const std::string& windowId);
    bool NavigateToBookmark(const std::string& windowId, const std::string& bookmarkName);
    std::string GetCurrentPageId(const std::string& windowId);
    int GetCurrentPageNumber(const std::string& windowId);
    
    // Bookmarks and table of contents
    bool AddBookmark(const std::string& windowId, const std::string& bookmarkName, const std::string& pageId);
    bool RemoveBookmark(const std::string& windowId, const std::string& bookmarkName);
    std::map<std::string, std::string> GetBookmarks(const std::string& windowId);
    bool SetTableOfContents(const std::string& windowId, const std::vector<std::pair<std::string, std::string>>& toc);
    std::vector<std::pair<std::string, std::string>> GetTableOfContents(const std::string& windowId);
    
    // Page transitions
    void SetPageTransitions(const std::string& windowId, const UCPageTransitionSettings& settings);
    UCPageTransitionSettings GetPageTransitions(const std::string& windowId);
    
    // Component management (updated for multi-page)
    // Component management (updated for multi-page)
    bool AddComponent(const std::string& windowId, const std::string& pageId, const std::shared_ptr<UltraCanvasComponent>& component);
    bool RemoveComponent(const std::string& windowId, const std::string& pageId, const std::string& componentId);
    std::shared_ptr<UCComponentData> GetComponentData(const std::string& windowId, const std::string& pageId, const std::string& componentId);
    
    // Legacy single-page component methods (for backwards compatibility)
    bool AddComponent(const std::string& windowId, const std::shared_ptr<UltraCanvasComponent>& component); // Adds to first page
    bool RemoveComponent(const std::string& windowId, const std::string& componentId); // Removes from all pages
    std::shared_ptr<UCComponentData> GetComponentData(const std::string& windowId, const std::string& componentId); // Gets from first page
    
    // Media resource management
    bool AddMediaResource(const UCMediaResource& resource);
    bool RemoveMediaResource(const std::string& resourceId);
    std::shared_ptr<UCMediaResource> GetMediaResource(const std::string& resourceId);
    std::vector<std::string> GetMediaResourceIds() const;
    
    // Template functionality
    bool SaveAsTemplate(const std::string& templatePath, const std::string& templateName);
    bool LoadFromTemplate(const std::string& templatePath);
    std::vector<std::string> GetAvailableTemplates(const std::string& templateDirectory);
    
    // Interactive document functionality
    bool SetFormField(const std::string& fieldId, const std::variant<std::string, int, double, bool>& value);
    std::variant<std::string, int, double, bool> GetFormField(const std::string& fieldId);
    bool ValidateForm();
    std::vector<std::string> GetFormValidationErrors();
    
    // Security and encryption
    bool SetPassword(const std::string& password);
    bool VerifyPassword(const std::string& password);
    void SetSecuritySettings(const UCSecuritySettings& settings);
    UCSecuritySettings GetSecuritySettings() const;
    
    // Document properties
    void SetMetadata(const UCDocumentMetadata& metadata);
    UCDocumentMetadata GetMetadata() const;
    
    // Serialization helpers
    std::string SerializeToXML() const;
    bool DeserializeFromXML(const std::string& xmlContent);
    std::string SerializeToJSON() const;
    bool DeserializeFromJSON(const std::string& jsonContent);
    
    // File format detection
    static bool IsValidUCDFile(const std::string& filePath);
    static UCDocumentType DetectDocumentType(const std::string& filePath);
    
    // Conversion utilities (updated for multi-page)
    std::shared_ptr<UltraCanvasWindow> ConvertToWindow(const std::string& windowId);
    bool ConvertFromWindow(const std::shared_ptr<UltraCanvasWindow>& window);
    
    // Multi-page document utilities
    bool CreateMultiPageDocument(const std::vector<std::shared_ptr<UltraCanvasWindow>>& pages, 
                                const std::string& documentTitle = "");
    std::vector<std::shared_ptr<UltraCanvasWindow>> ConvertToWindows();
    bool MergeDocuments(const std::shared_ptr<UltraCanvasDocument>& otherDocument);
    bool SplitDocument(const std::vector<std::string>& pageIds, 
                      std::shared_ptr<UltraCanvasDocument>& newDocument);
    
    // Page template support
    bool CreatePageFromTemplate(const std::string& windowId, const std::string& templateName, 
                               int insertPosition = -1);
    bool SavePageAsTemplate(const std::string& windowId, const std::string& pageId, 
                           const std::string& templateName);

private:
    // Internal data
    UCDocumentMetadata Metadata;
    std::vector<std::shared_ptr<UCWindowData>> Windows;
    std::map<std::string, std::shared_ptr<UCMediaResource>> MediaResources;
    UCSecuritySettings SecuritySettings;
    std::map<std::string, std::variant<std::string, int, double, bool>> FormData;
    
    // Internal helper methods (updated for multi-page)
    bool CompressData(const std::vector<uint8_t>& input, std::vector<uint8_t>& output, UCCompressionType type);
    bool DecompressData(const std::vector<uint8_t>& input, std::vector<uint8_t>& output, UCCompressionType type);
    bool EncryptData(const std::vector<uint8_t>& input, std::vector<uint8_t>& output, const std::string& password);
    bool DecryptData(const std::vector<uint8_t>& input, std::vector<uint8_t>& output, const std::string& password);
    
    std::string GeneratePasswordHash(const std::string& password, const std::string& salt);
    std::string GenerateSalt();
    std::string GetCurrentDateTime();
    std::string EscapeXML(const std::string& input);
    std::string Base64Encode(const std::vector<uint8_t>& data);
    std::vector<uint8_t> Base64Decode(const std::string& input);
    
    // Component serialization helpers
    std::shared_ptr<UCComponentData> SerializeComponent(const std::shared_ptr<UltraCanvasComponent>& component);
    std::shared_ptr<UltraCanvasComponent> DeserializeComponent(const std::shared_ptr<UCComponentData>& componentData);
    
    // XML/JSON helpers (updated for multi-page)
    std::string WindowDataToXML(const std::shared_ptr<UCWindowData>& windowData) const;
    std::shared_ptr<UCWindowData> WindowDataFromXML(const std::string& xmlContent);
    std::string PageDataToXML(const std::shared_ptr<UCPageData>& pageData) const;
    std::shared_ptr<UCPageData> PageDataFromXML(const std::string& xmlContent);
    std::string ComponentDataToXML(const std::shared_ptr<UCComponentData>& componentData) const;
    std::shared_ptr<UCComponentData> ComponentDataFromXML(const std::string& xmlContent);
    
    // Page navigation helpers
    void UpdateNavigationHistory(const std::string& windowId, const std::string& pageId);
    void ValidatePageOrder(const std::string& windowId);
    std::string GenerateUniquePageId();
};

// Template manager class
class UCTemplateManager {
public:
    static bool RegisterTemplate(const std::string& templateName, const std::string& templatePath);
    static bool UnregisterTemplate(const std::string& templateName);
    static std::vector<std::string> GetRegisteredTemplates();
    static std::string GetTemplatePath(const std::string& templateName);
    
    static bool CreateTemplateFromWindow(const std::shared_ptr<UltraCanvasWindow>& window, 
                                        const std::string& templateName, 
                                        const std::string& description = "");
    
    static std::shared_ptr<UltraCanvasWindow> CreateWindowFromTemplate(const std::string& templateName);
    
private:
    static std::map<std::string, std::string> RegisteredTemplates;
};

// Interactive document renderer (updated for multi-page)
class UCInteractiveRenderer {
public:
    explicit UCInteractiveRenderer(std::shared_ptr<UltraCanvasDocument> document);
    ~UCInteractiveRenderer();
    
    bool RenderToWindow(std::shared_ptr<UltraCanvasWindow> targetWindow);
    bool HandleInteractiveEvent(const std::string& componentId, const std::string& eventType, 
                               const std::map<std::string, std::variant<std::string, int, double, bool>>& eventData);
    
    // Page navigation
    bool NavigateToPage(const std::string& windowId, int pageNumber);
    bool NavigateToPage(const std::string& windowId, const std::string& pageId);
    bool NavigateNext(const std::string& windowId);
    bool NavigatePrevious(const std::string& windowId);
    bool NavigateToLink(const std::string& linkTarget);
    bool NavigateToBookmark(const std::string& windowId, const std::string& bookmarkName);
    
    // Page transition effects
    void SetTransitionDuration(int durationMs);
    void SetTransitionType(UCPageTransition transition);
    void EnableTransitionAnimations(bool enable);
    
    // Multi-page rendering
    bool RenderCurrentPage();
    bool RenderPage(const std::string& pageId);
    bool PreloadPage(const std::string& pageId); // For smooth transitions
    void ClearPageCache();
    
    // Page information
    int GetCurrentPageNumber(const std::string& windowId);
    int GetTotalPageCount(const std::string& windowId);
    std::string GetCurrentPageId(const std::string& windowId);
    std::vector<std::string> GetPageIds(const std::string& windowId);
    
    // Form handling (updated for multi-page)
    bool SubmitForm(const std::string& formId);
    bool ResetForm(const std::string& formId);
    bool ValidateField(const std::string& fieldId);
    bool ValidateCurrentPage();
    bool ValidateAllPages();
    
    // Search and navigation
    std::vector<std::pair<std::string, int>> SearchText(const std::string& searchTerm); // Returns pageId, occurrence count
    bool HighlightSearchResults(const std::string& searchTerm);
    void ClearHighlights();
    
    // Presentation mode
    bool EnterPresentationMode();
    bool ExitPresentationMode();
    bool IsPresentationMode() const;
    
    // Page thumbnails
    bool GeneratePageThumbnail(const std::string& pageId, int thumbnailWidth, int thumbnailHeight);
    std::vector<uint8_t> GetPageThumbnail(const std::string& pageId);
    
private:
    std::shared_ptr<UltraCanvasDocument> Document;
    std::shared_ptr<UltraCanvasWindow> RenderWindow;
    std::map<std::string, std::function<void(const std::map<std::string, std::variant<std::string, int, double, bool>>&)>> EventHandlers;
    
    // Multi-page rendering state
    std::string CurrentWindowId;
    std::map<std::string, std::shared_ptr<UltraCanvasWindow>> PageCache; // pageId -> rendered window
    UCPageTransitionSettings TransitionSettings;
    bool TransitionsEnabled = true;
    bool PresentationMode = false;
    
    // Page transition animation
    bool AnimatePageTransition(const std::string& fromPageId, const std::string& toPageId);
    void ApplyPageTransition(UCPageTransition transition, float progress);
    
    // Search functionality
    std::map<std::string, std::vector<std::pair<int, int>>> SearchResults; // pageId -> [(start, end), ...]
    std::string CurrentSearchTerm;
};

// Utility functions
namespace UCDocumentUtils {
    // File operations
    std::vector<uint8_t> ReadBinaryFile(const std::string& filePath);
    bool WriteBinaryFile(const std::string& filePath, const std::vector<uint8_t>& data);
    
    // MIME type detection
    std::string DetectMimeType(const std::string& filePath);
    std::string DetectMimeTypeFromData(const std::vector<uint8_t>& data);
    
    // Resource embedding
    bool EmbedResource(UCMediaResource& resource, const std::string& filePath);
    bool ExtractResource(const UCMediaResource& resource, const std::string& outputPath);
    
    // Validation (updated for multi-page)
    bool ValidateComponentData(const std::shared_ptr<UCComponentData>& componentData);
    bool ValidatePageData(const std::shared_ptr<UCPageData>& pageData);
    bool ValidateWindowData(const std::shared_ptr<UCWindowData>& windowData);
    bool ValidateDocument(const std::shared_ptr<UltraCanvasDocument>& document);
    
    // Multi-page utilities
    std::shared_ptr<UCPageData> CreateEmptyPage(const std::string& pageId, const std::string& pageName = "");
    bool CopyPage(const std::shared_ptr<UCPageData>& sourcePage, std::shared_ptr<UCPageData>& targetPage);
    bool ClonePage(const std::shared_ptr<UltraCanvasDocument>& document, const std::string& windowId, 
                   const std::string& sourcePageId, const std::string& newPageId, const std::string& newPageName = "");
    
    // Page layout utilities
    void CalculatePageLayout(const std::shared_ptr<UCPageData>& pageData, int& contentWidth, int& contentHeight);
    bool OptimizePageLayout(std::shared_ptr<UCPageData>& pageData);
    
    // Content analysis
    int CountTextCharacters(const std::shared_ptr<UCPageData>& pageData);
    int CountImages(const std::shared_ptr<UCPageData>& pageData);
    int CountFormElements(const std::shared_ptr<UCPageData>& pageData);
    std::vector<std::string> ExtractLinks(const std::shared_ptr<UCPageData>& pageData);
    
    // Page conversion utilities
    bool ConvertPageToPDF(const std::shared_ptr<UCPageData>& pageData, const std::string& outputPath);
    bool ConvertPageToImage(const std::shared_ptr<UCPageData>& pageData, const std::string& outputPath, 
                           const std::string& format = "PNG");
    
    // Accessibility helpers
    bool ValidateAccessibility(const std::shared_ptr<UCPageData>& pageData);
    std::vector<std::string> GetAccessibilityIssues(const std::shared_ptr<UCPageData>& pageData);
    bool GenerateAltTextSuggestions(std::shared_ptr<UCPageData>& pageData);
}

} // namespace UltraCanvas
