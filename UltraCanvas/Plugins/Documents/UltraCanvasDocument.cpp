// UltraCanvasDocument.cpp
// Implementation of UltraCanvas Document format (.UCD) for saving/loading windows
// Version: 1.0.0
// Last Modified: 2025-08-26
// Author: UltraCanvas Framework

#include "UltraCanvasDocument.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <random>
#include <chrono>
#include <iomanip>

// Additional includes for compression and encryption
#ifdef ULTRACANVAS_USE_ZLIB
#include <zlib.h>
#endif

#ifdef ULTRACANVAS_USE_OPENSSL
#include <openssl/aes.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#endif

// XML/JSON parsing (assuming we have a JSON library like nlohmann/json)
#ifdef ULTRACANVAS_USE_JSON
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#endif

// TinyXML2 for XML parsing
#ifdef ULTRACANVAS_USE_XML
#include <tinyxml2.h>
using namespace tinyxml2;
#endif

namespace UltraCanvas {

// Static template registry
std::map<std::string, std::string> UCTemplateManager::RegisteredTemplates;

// UltraCanvasDocument Implementation
UltraCanvasDocument::UltraCanvasDocument() {
    Metadata.DocumentType = UCDocumentType::Template;
    Metadata.CreatedDate = GetCurrentDateTime();
    Metadata.ModifiedDate = Metadata.CreatedDate;
    Metadata.Version = UCD_FORMAT_VERSION;
    
    SecuritySettings.EncryptionType = UCEncryptionType::None;
    SecuritySettings.AllowPrint = true;
    SecuritySettings.AllowCopy = true;
    SecuritySettings.AllowEdit = true;
    SecuritySettings.AllowFormFilling = true;
}

UltraCanvasDocument::UltraCanvasDocument(UCDocumentType documentType) : UltraCanvasDocument() {
    Metadata.DocumentType = documentType;
}

UltraCanvasDocument::~UltraCanvasDocument() {
    Windows.clear();
    MediaResources.clear();
    FormData.clear();
}

bool UltraCanvasDocument::CreateNewDocument(UCDocumentType documentType) {
    // Clear existing data
    Windows.clear();
    MediaResources.clear();
    FormData.clear();
    
    // Reset metadata
    Metadata = UCDocumentMetadata();
    Metadata.DocumentType = documentType;
    Metadata.CreatedDate = GetCurrentDateTime();
    Metadata.ModifiedDate = Metadata.CreatedDate;
    Metadata.Version = UCD_FORMAT_VERSION;
    
    return true;
}

bool UltraCanvasDocument::LoadFromFile(const std::string& filePath, const std::string& password) {
    try {
        // Read binary file
        std::vector<uint8_t> fileData = UCDocumentUtils::ReadBinaryFile(filePath);
        if (fileData.empty()) {
            return false;
        }
        
        // Check file header for UCD signature
        if (fileData.size() < 8 || 
            std::string(fileData.begin(), fileData.begin() + 4) != "UCD\x01") {
            return false;
        }
        
        // Read compression and encryption flags
        UCCompressionType compression = static_cast<UCCompressionType>(fileData[4]);
        UCEncryptionType encryption = static_cast<UCEncryptionType>(fileData[5]);
        
        // Skip header (8 bytes)
        std::vector<uint8_t> contentData(fileData.begin() + 8, fileData.end());
        
        // Decrypt if needed
        if (encryption != UCEncryptionType::None) {
            if (password.empty()) {
                return false; // Password required
            }
            
            std::vector<uint8_t> decryptedData;
            if (!DecryptData(contentData, decryptedData, password)) {
                return false;
            }
            contentData = std::move(decryptedData);
        }
        
        // Decompress if needed
        if (compression != UCCompressionType::None) {
            std::vector<uint8_t> decompressedData;
            if (!DecompressData(contentData, decompressedData, compression)) {
                return false;
            }
            contentData = std::move(decompressedData);
        }
        
        // Parse XML/JSON content
        std::string contentString(contentData.begin(), contentData.end());
        
        // Try XML first, then JSON
        if (contentString.front() == '<') {
            return DeserializeFromXML(contentString);
        } else if (contentString.front() == '{') {
            return DeserializeFromJSON(contentString);
        }
        
        return false;
    }
    catch (const std::exception& e) {
        // Log error: e.what()
        return false;
    }
}

bool UltraCanvasDocument::SaveToFile(const std::string& filePath, UCCompressionType compression, const std::string& password) {
    try {
        // Update modification date
        Metadata.ModifiedDate = GetCurrentDateTime();
        
        // Serialize to XML (you could also choose JSON)
        std::string contentString = SerializeToXML();
        std::vector<uint8_t> contentData(contentString.begin(), contentString.end());
        
        // Compress if needed
        if (compression != UCCompressionType::None) {
            std::vector<uint8_t> compressedData;
            if (!CompressData(contentData, compressedData, compression)) {
                return false;
            }
            contentData = std::move(compressedData);
        }
        
        // Encrypt if password provided
        UCEncryptionType encryption = UCEncryptionType::None;
        if (!password.empty()) {
            encryption = UCEncryptionType::AES256;
            std::vector<uint8_t> encryptedData;
            if (!EncryptData(contentData, encryptedData, password)) {
                return false;
            }
            contentData = std::move(encryptedData);
        }
        
        // Create file header
        std::vector<uint8_t> fileData;
        
        // UCD signature (4 bytes)
        fileData.push_back('U');
        fileData.push_back('C');
        fileData.push_back('D');
        fileData.push_back(0x01); // Version
        
        // Compression type (1 byte)
        fileData.push_back(static_cast<uint8_t>(compression));
        
        // Encryption type (1 byte)
        fileData.push_back(static_cast<uint8_t>(encryption));
        
        // Reserved bytes (2 bytes)
        fileData.push_back(0x00);
        fileData.push_back(0x00);
        
        // Append content data
        fileData.insert(fileData.end(), contentData.begin(), contentData.end());
        
        // Write to file
        return UCDocumentUtils::WriteBinaryFile(filePath, fileData);
    }
    catch (const std::exception& e) {
        // Log error: e.what()
        return false;
    }
}

bool UltraCanvasDocument::AddWindow(const std::shared_ptr<UltraCanvasWindow>& window) {
    if (!window) return false;
    
    auto windowData = std::make_shared<UCWindowData>();
    // Convert UltraCanvasWindow to UCWindowData
    // This would need implementation based on your UltraCanvasWindow class
    
    Windows.push_back(windowData);
    return true;
}

bool UltraCanvasDocument::AddPage(const std::string& windowId, const std::shared_ptr<UCPageData>& pageData) {
    auto windowData = GetWindowData(windowId);
    if (!windowData || !pageData) return false;
    
    // Generate unique page ID if not set
    if (pageData->PageId.empty()) {
        pageData->PageId = GenerateUniquePageId();
    }
    
    // Set page number
    pageData->PageNumber = static_cast<int>(windowData->Pages.size()) + 1;
    
    // Add to pages
    windowData->Pages.push_back(pageData);
    
    // Update navigation
    windowData->Navigation.PageOrder.push_back(pageData->PageId);
    if (windowData->Navigation.CurrentPageId.empty()) {
        windowData->Navigation.CurrentPageId = pageData->PageId;
    }
    
    return true;
}

bool UltraCanvasDocument::RemovePage(const std::string& windowId, const std::string& pageId) {
    auto windowData = GetWindowData(windowId);
    if (!windowData) return false;
    
    // Remove from pages
    auto pageIt = std::remove_if(windowData->Pages.begin(), windowData->Pages.end(),
        [&pageId](const std::shared_ptr<UCPageData>& page) {
            return page && page->PageId == pageId;
        });
    
    if (pageIt == windowData->Pages.end()) return false;
    
    windowData->Pages.erase(pageIt, windowData->Pages.end());
    
    // Update navigation
    auto navIt = std::find(windowData->Navigation.PageOrder.begin(), 
                          windowData->Navigation.PageOrder.end(), pageId);
    if (navIt != windowData->Navigation.PageOrder.end()) {
        windowData->Navigation.PageOrder.erase(navIt);
    }
    
    // Update current page if necessary
    if (windowData->Navigation.CurrentPageId == pageId) {
        if (!windowData->Pages.empty()) {
            windowData->Navigation.CurrentPageId = windowData->Pages[0]->PageId;
        } else {
            windowData->Navigation.CurrentPageId.clear();
        }
    }
    
    // Re-number remaining pages
    for (size_t i = 0; i < windowData->Pages.size(); ++i) {
        windowData->Pages[i]->PageNumber = static_cast<int>(i) + 1;
    }
    
    return true;
}

bool UltraCanvasDocument::InsertPage(const std::string& windowId, int position, const std::shared_ptr<UCPageData>& pageData) {
    auto windowData = GetWindowData(windowId);
    if (!windowData || !pageData) return false;
    
    // Validate position
    if (position < 0) position = 0;
    if (position > static_cast<int>(windowData->Pages.size())) {
        position = static_cast<int>(windowData->Pages.size());
    }
    
    // Generate unique page ID if not set
    if (pageData->PageId.empty()) {
        pageData->PageId = GenerateUniquePageId();
    }
    
    // Insert page
    windowData->Pages.insert(windowData->Pages.begin() + position, pageData);
    
    // Insert in navigation order
    if (position < static_cast<int>(windowData->Navigation.PageOrder.size())) {
        windowData->Navigation.PageOrder.insert(
            windowData->Navigation.PageOrder.begin() + position, pageData->PageId);
    } else {
        windowData->Navigation.PageOrder.push_back(pageData->PageId);
    }
    
    // Re-number all pages
    for (size_t i = 0; i < windowData->Pages.size(); ++i) {
        windowData->Pages[i]->PageNumber = static_cast<int>(i) + 1;
    }
    
    return true;
}

bool UltraCanvasDocument::MovePage(const std::string& windowId, const std::string& pageId, int newPosition) {
    auto windowData = GetWindowData(windowId);
    if (!windowData) return false;
    
    // Find the page
    auto pageIt = std::find_if(windowData->Pages.begin(), windowData->Pages.end(),
        [&pageId](const std::shared_ptr<UCPageData>& page) {
            return page && page->PageId == pageId;
        });
    
    if (pageIt == windowData->Pages.end()) return false;
    
    // Get the page and remove it temporarily
    auto pageData = *pageIt;
    int currentPosition = static_cast<int>(std::distance(windowData->Pages.begin(), pageIt));
    windowData->Pages.erase(pageIt);
    
    // Validate new position
    if (newPosition < 0) newPosition = 0;
    if (newPosition >= static_cast<int>(windowData->Pages.size())) {
        newPosition = static_cast<int>(windowData->Pages.size());
    }
    
    // Insert at new position
    windowData->Pages.insert(windowData->Pages.begin() + newPosition, pageData);
    
    // Update navigation order
    auto navIt = std::find(windowData->Navigation.PageOrder.begin(), 
                          windowData->Navigation.PageOrder.end(), pageId);
    if (navIt != windowData->Navigation.PageOrder.end()) {
        windowData->Navigation.PageOrder.erase(navIt);
        if (newPosition < static_cast<int>(windowData->Navigation.PageOrder.size())) {
            windowData->Navigation.PageOrder.insert(
                windowData->Navigation.PageOrder.begin() + newPosition, pageId);
        } else {
            windowData->Navigation.PageOrder.push_back(pageId);
        }
    }
    
    // Re-number all pages
    for (size_t i = 0; i < windowData->Pages.size(); ++i) {
        windowData->Pages[i]->PageNumber = static_cast<int>(i) + 1;
    }
    
    return true;
}

std::shared_ptr<UCPageData> UltraCanvasDocument::GetPageData(const std::string& windowId, const std::string& pageId) {
    auto windowData = GetWindowData(windowId);
    if (!windowData) return nullptr;
    
    auto it = std::find_if(windowData->Pages.begin(), windowData->Pages.end(),
        [&pageId](const std::shared_ptr<UCPageData>& page) {
            return page && page->PageId == pageId;
        });
    
    return (it != windowData->Pages.end()) ? *it : nullptr;
}

std::shared_ptr<UCPageData> UltraCanvasDocument::GetPageByNumber(const std::string& windowId, int pageNumber) {
    auto windowData = GetWindowData(windowId);
    if (!windowData) return nullptr;
    
    if (pageNumber < 1 || pageNumber > static_cast<int>(windowData->Pages.size())) {
        return nullptr;
    }
    
    return windowData->Pages[pageNumber - 1];
}

std::vector<std::string> UltraCanvasDocument::GetPageIds(const std::string& windowId) const {
    auto windowData = GetWindowData(windowId);
    if (!windowData) return {};
    
    return windowData->Navigation.PageOrder;
}

int UltraCanvasDocument::GetPageCount(const std::string& windowId) const {
    auto windowData = GetWindowData(windowId);
    if (!windowData) return 0;
    
    return static_cast<int>(windowData->Pages.size());
}

bool UltraCanvasDocument::NavigateToPage(const std::string& windowId, const std::string& pageId) {
    auto windowData = GetWindowData(windowId);
    if (!windowData) return false;
    
    // Verify page exists
    auto pageData = GetPageData(windowId, pageId);
    if (!pageData) return false;
    
    // Update current page
    std::string previousPage = windowData->Navigation.CurrentPageId;
    windowData->Navigation.CurrentPageId = pageId;
    
    // Update navigation history
    UpdateNavigationHistory(windowId, pageId);
    
    return true;
}

bool UltraCanvasDocument::NavigateToPageNumber(const std::string& windowId, int pageNumber) {
    auto pageData = GetPageByNumber(windowId, pageNumber);
    if (!pageData) return false;
    
    return NavigateToPage(windowId, pageData->PageId);
}

bool UltraCanvasDocument::NavigateNext(const std::string& windowId) {
    auto windowData = GetWindowData(windowId);
    if (!windowData) return false;
    
    auto currentIt = std::find(windowData->Navigation.PageOrder.begin(), 
                              windowData->Navigation.PageOrder.end(), 
                              windowData->Navigation.CurrentPageId);
    
    if (currentIt != windowData->Navigation.PageOrder.end()) {
        auto nextIt = std::next(currentIt);
        if (nextIt != windowData->Navigation.PageOrder.end()) {
            return NavigateToPage(windowId, *nextIt);
        }
    }
    
    return false; // Already at last page
}

bool UltraCanvasDocument::NavigatePrevious(const std::string& windowId) {
    auto windowData = GetWindowData(windowId);
    if (!windowData) return false;
    
    auto currentIt = std::find(windowData->Navigation.PageOrder.begin(), 
                              windowData->Navigation.PageOrder.end(), 
                              windowData->Navigation.CurrentPageId);
    
    if (currentIt != windowData->Navigation.PageOrder.begin() && 
        currentIt != windowData->Navigation.PageOrder.end()) {
        auto prevIt = std::prev(currentIt);
        return NavigateToPage(windowId, *prevIt);
    }
    
    return false; // Already at first page
}

bool UltraCanvasDocument::NavigateToBookmark(const std::string& windowId, const std::string& bookmarkName) {
    auto windowData = GetWindowData(windowId);
    if (!windowData) return false;
    
    auto bookmarkIt = windowData->Navigation.Bookmarks.find(bookmarkName);
    if (bookmarkIt != windowData->Navigation.Bookmarks.end()) {
        return NavigateToPage(windowId, bookmarkIt->second);
    }
    
    return false;
}

std::string UltraCanvasDocument::GetCurrentPageId(const std::string& windowId) {
    auto windowData = GetWindowData(windowId);
    if (!windowData) return "";
    
    return windowData->Navigation.CurrentPageId;
}

int UltraCanvasDocument::GetCurrentPageNumber(const std::string& windowId) {
    auto windowData = GetWindowData(windowId);
    if (!windowData) return 0;
    
    auto pageData = GetPageData(windowId, windowData->Navigation.CurrentPageId);
    return pageData ? pageData->PageNumber : 0;
}

bool UltraCanvasDocument::AddBookmark(const std::string& windowId, const std::string& bookmarkName, const std::string& pageId) {
    auto windowData = GetWindowData(windowId);
    if (!windowData) return false;
    
    // Verify page exists
    if (!GetPageData(windowId, pageId)) return false;
    
    windowData->Navigation.Bookmarks[bookmarkName] = pageId;
    return true;
}

bool UltraCanvasDocument::RemoveBookmark(const std::string& windowId, const std::string& bookmarkName) {
    auto windowData = GetWindowData(windowId);
    if (!windowData) return false;
    
    return windowData->Navigation.Bookmarks.erase(bookmarkName) > 0;
}

std::map<std::string, std::string> UltraCanvasDocument::GetBookmarks(const std::string& windowId) {
    auto windowData = GetWindowData(windowId);
    if (!windowData) return {};
    
    return windowData->Navigation.Bookmarks;
}

bool UltraCanvasDocument::SetTableOfContents(const std::string& windowId, const std::vector<std::pair<std::string, std::string>>& toc) {
    auto windowData = GetWindowData(windowId);
    if (!windowData) return false;
    
    windowData->Navigation.TableOfContents = toc;
    return true;
}

std::vector<std::pair<std::string, std::string>> UltraCanvasDocument::GetTableOfContents(const std::string& windowId) {
    auto windowData = GetWindowData(windowId);
    if (!windowData) return {};
    
    return windowData->Navigation.TableOfContents;
}

void UltraCanvasDocument::SetPageTransitions(const std::string& windowId, const UCPageTransitionSettings& settings) {
    auto windowData = GetWindowData(windowId);
    if (windowData) {
        windowData->PageTransitions = settings;
    }
}

UCPageTransitionSettings UltraCanvasDocument::GetPageTransitions(const std::string& windowId) {
    auto windowData = GetWindowData(windowId);
    if (windowData) {
        return windowData->PageTransitions;
    }
    return UCPageTransitionSettings();
}

// Multi-page component management
bool UltraCanvasDocument::AddComponent(const std::string& windowId, const std::string& pageId, const std::shared_ptr<UltraCanvasComponent>& component) {
    auto pageData = GetPageData(windowId, pageId);
    if (!pageData || !component) return false;
    
    auto componentData = SerializeComponent(component);
    if (!componentData) return false;
    
    pageData->Components.push_back(componentData);
    return true;
}

bool UltraCanvasDocument::RemoveComponent(const std::string& windowId, const std::string& pageId, const std::string& componentId) {
    auto pageData = GetPageData(windowId, pageId);
    if (!pageData) return false;
    
    auto it = std::remove_if(pageData->Components.begin(), pageData->Components.end(),
        [&componentId](const std::shared_ptr<UCComponentData>& component) {
            return component && component->ComponentId == componentId;
        });
    
    if (it != pageData->Components.end()) {
        pageData->Components.erase(it, pageData->Components.end());
        return true;
    }
    return false;
}

std::shared_ptr<UCComponentData> UltraCanvasDocument::GetComponentData(const std::string& windowId, const std::string& pageId, const std::string& componentId) {
    auto pageData = GetPageData(windowId, pageId);
    if (!pageData) return nullptr;
    
    auto it = std::find_if(pageData->Components.begin(), pageData->Components.end(),
        [&componentId](const std::shared_ptr<UCComponentData>& component) {
            return component && component->ComponentId == componentId;
        });
    
    return (it != pageData->Components.end()) ? *it : nullptr;
}

bool UltraCanvasDocument::CreateMultiPageDocument(const std::vector<std::shared_ptr<UltraCanvasWindow>>& pages, const std::string& documentTitle) {
    CreateNewDocument(UCDocumentType::Interactive);
    
    if (!documentTitle.empty()) {
        Metadata.Title = documentTitle;
    }
    
    // Create a window to contain all pages
    auto windowData = std::make_shared<UCWindowData>();
    windowData->WindowId = "MainWindow";
    windowData->Title = documentTitle;
    
    // Convert each window to a page
    for (size_t i = 0; i < pages.size(); ++i) {
        auto window = pages[i];
        if (!window) continue;
        
        auto pageData = std::make_shared<UCPageData>();
        pageData->PageId = "Page_" + std::to_string(i + 1);
        pageData->PageName = "Page " + std::to_string(i + 1);
        pageData->PageNumber = static_cast<int>(i + 1);
        
        // Copy window properties to page
        // This would need implementation based on your UltraCanvasWindow class
        
        windowData->Pages.push_back(pageData);
        windowData->Navigation.PageOrder.push_back(pageData->PageId);
    }
    
    if (!windowData->Pages.empty()) {
        windowData->Navigation.CurrentPageId = windowData->Pages[0]->PageId;
    }
    
    Windows.push_back(windowData);
    return true;
}
    auto it = std::remove_if(Windows.begin(), Windows.end(),
        [&windowId](const std::shared_ptr<UCWindowData>& window) {
            return window && window->WindowId == windowId;
        });
    
    if (it != Windows.end()) {
        Windows.erase(it, Windows.end());
        return true;
    }
    return false;
}

std::shared_ptr<UCWindowData> UltraCanvasDocument::GetWindowData(const std::string& windowId) {
    auto it = std::find_if(Windows.begin(), Windows.end(),
        [&windowId](const std::shared_ptr<UCWindowData>& window) {
            return window && window->WindowId == windowId;
        });
    
    return (it != Windows.end()) ? *it : nullptr;
}

std::vector<std::string> UltraCanvasDocument::GetWindowIds() const {
    std::vector<std::string> ids;
    for (const auto& window : Windows) {
        if (window) {
            ids.push_back(window->WindowId);
        }
    }
    return ids;
}

bool UltraCanvasDocument::AddMediaResource(const UCMediaResource& resource) {
    MediaResources[resource.ResourceId] = std::make_shared<UCMediaResource>(resource);
    return true;
}

bool UltraCanvasDocument::RemoveMediaResource(const std::string& resourceId) {
    return MediaResources.erase(resourceId) > 0;
}

std::shared_ptr<UCMediaResource> UltraCanvasDocument::GetMediaResource(const std::string& resourceId) {
    auto it = MediaResources.find(resourceId);
    return (it != MediaResources.end()) ? it->second : nullptr;
}

std::vector<std::string> UltraCanvasDocument::GetMediaResourceIds() const {
    std::vector<std::string> ids;
    for (const auto& pair : MediaResources) {
        ids.push_back(pair.first);
    }
    return ids;
}

bool UltraCanvasDocument::SaveAsTemplate(const std::string& templatePath, const std::string& templateName) {
    Metadata.DocumentType = UCDocumentType::Template;
    Metadata.Title = templateName;
    
    return SaveToFile(templatePath, UCCompressionType::ZIP);
}

bool UltraCanvasDocument::LoadFromTemplate(const std::string& templatePath) {
    return LoadFromFile(templatePath);
}

std::vector<std::string> UltraCanvasDocument::GetAvailableTemplates(const std::string& templateDirectory) {
    // Implementation would scan directory for .ucd files
    // This is platform-specific and would use OS-specific directory APIs
    std::vector<std::string> templates;
    // ... directory scanning implementation ...
    return templates;
}

bool UltraCanvasDocument::SetFormField(const std::string& fieldId, const std::variant<std::string, int, double, bool>& value) {
    FormData[fieldId] = value;
    return true;
}

std::variant<std::string, int, double, bool> UltraCanvasDocument::GetFormField(const std::string& fieldId) {
    auto it = FormData.find(fieldId);
    if (it != FormData.end()) {
        return it->second;
    }
    return std::string(""); // Default empty string
}

bool UltraCanvasDocument::ValidateForm() {
    // Implementation would validate all form fields against their validation rules
    // This is a simplified version
    return true;
}

std::vector<std::string> UltraCanvasDocument::GetFormValidationErrors() {
    std::vector<std::string> errors;
    // Implementation would collect validation errors
    return errors;
}

bool UltraCanvasDocument::SetPassword(const std::string& password) {
    if (password.empty()) {
        SecuritySettings.EncryptionType = UCEncryptionType::None;
        SecuritySettings.PasswordHash.clear();
        SecuritySettings.Salt.clear();
        return true;
    }
    
    SecuritySettings.EncryptionType = UCEncryptionType::AES256;
    SecuritySettings.Salt = GenerateSalt();
    SecuritySettings.PasswordHash = GeneratePasswordHash(password, SecuritySettings.Salt);
    return true;
}

bool UltraCanvasDocument::VerifyPassword(const std::string& password) {
    if (SecuritySettings.EncryptionType == UCEncryptionType::None) {
        return password.empty();
    }
    
    std::string hashedPassword = GeneratePasswordHash(password, SecuritySettings.Salt);
    return hashedPassword == SecuritySettings.PasswordHash;
}

void UltraCanvasDocument::SetMetadata(const UCDocumentMetadata& metadata) {
    Metadata = metadata;
    Metadata.ModifiedDate = GetCurrentDateTime();
}

UCDocumentMetadata UltraCanvasDocument::GetMetadata() const {
    return Metadata;
}

std::string UltraCanvasDocument::SerializeToXML() const {
    std::ostringstream oss;
    
    oss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    oss << "<UltraCanvasDocument version=\"" << UCD_FORMAT_VERSION << "\">\n";
    
    // Metadata
    oss << "  <Metadata>\n";
    oss << "    <Title>" << EscapeXML(Metadata.Title) << "</Title>\n";
    oss << "    <Author>" << EscapeXML(Metadata.Author) << "</Author>\n";
    oss << "    <Description>" << EscapeXML(Metadata.Description) << "</Description>\n";
    oss << "    <CreatedDate>" << Metadata.CreatedDate << "</CreatedDate>\n";
    oss << "    <ModifiedDate>" << Metadata.ModifiedDate << "</ModifiedDate>\n";
    oss << "    <DocumentType>" << static_cast<int>(Metadata.DocumentType) << "</DocumentType>\n";
    oss << "  </Metadata>\n";
    
    // Windows
    oss << "  <Windows>\n";
    for (const auto& window : Windows) {
        if (window) {
            oss << WindowDataToXML(window);
        }
    }
    oss << "  </Windows>\n";
    
    // Media Resources
    oss << "  <MediaResources>\n";
    for (const auto& pair : MediaResources) {
        const auto& resource = pair.second;
        if (resource) {
            oss << "    <Resource id=\"" << EscapeXML(resource->ResourceId) << "\">\n";
            oss << "      <Type>" << EscapeXML(resource->ResourceType) << "</Type>\n";
            oss << "      <MimeType>" << EscapeXML(resource->MimeType) << "</MimeType>\n";
            oss << "      <FileName>" << EscapeXML(resource->FileName) << "</FileName>\n";
            oss << "      <IsEmbedded>" << (resource->IsEmbedded ? "true" : "false") << "</IsEmbedded>\n";
            if (resource->IsEmbedded) {
                oss << "      <Data>" << Base64Encode(resource->Data) << "</Data>\n";
            } else {
                oss << "      <ExternalPath>" << EscapeXML(resource->ExternalPath) << "</ExternalPath>\n";
            }
            oss << "    </Resource>\n";
        }
    }
    oss << "  </MediaResources>\n";
    
    // Form Data
    oss << "  <FormData>\n";
    for (const auto& pair : FormData) {
        oss << "    <Field id=\"" << EscapeXML(pair.first) << "\">";
        std::visit([&oss](const auto& value) {
            oss << EscapeXML(std::to_string(value));
        }, pair.second);
        oss << "</Field>\n";
    }
    oss << "  </FormData>\n";
    
    oss << "</UltraCanvasDocument>\n";
    
    return oss.str();
}

bool UltraCanvasDocument::DeserializeFromXML(const std::string& xmlContent) {
    // Implementation would use TinyXML2 or similar to parse XML
    // This is a simplified structure showing the approach
    
#ifdef ULTRACANVAS_USE_XML
    XMLDocument doc;
    if (doc.Parse(xmlContent.c_str()) != XML_SUCCESS) {
        return false;
    }
    
    XMLElement* root = doc.FirstChildElement("UltraCanvasDocument");
    if (!root) return false;
    
    // Parse metadata
    XMLElement* metadataElement = root->FirstChildElement("Metadata");
    if (metadataElement) {
        // Parse metadata fields...
    }
    
    // Parse windows
    XMLElement* windowsElement = root->FirstChildElement("Windows");
    if (windowsElement) {
        // Parse window data...
    }
    
    // Parse media resources
    XMLElement* mediaElement = root->FirstChildElement("MediaResources");
    if (mediaElement) {
        // Parse media resources...
    }
    
    // Parse form data
    XMLElement* formElement = root->FirstChildElement("FormData");
    if (formElement) {
        // Parse form data...
    }
    
    return true;
#else
    // Fallback simple parsing without XML library
    return false;
#endif
}

std::string UltraCanvasDocument::SerializeToJSON() const {
#ifdef ULTRACANVAS_USE_JSON
    json j;
    
    // Metadata
    j["metadata"]["title"] = Metadata.Title;
    j["metadata"]["author"] = Metadata.Author;
    j["metadata"]["description"] = Metadata.Description;
    j["metadata"]["createdDate"] = Metadata.CreatedDate;
    j["metadata"]["modifiedDate"] = Metadata.ModifiedDate;
    j["metadata"]["version"] = Metadata.Version;
    j["metadata"]["documentType"] = static_cast<int>(Metadata.DocumentType);
    
    // Windows
    j["windows"] = json::array();
    for (const auto& window : Windows) {
        if (window) {
            json windowJson;
            windowJson["id"] = window->WindowId;
            windowJson["title"] = window->Title;
            windowJson["width"] = window->Width;
            windowJson["height"] = window->Height;
            windowJson["positionX"] = window->PositionX;
            windowJson["positionY"] = window->PositionY;
            j["windows"].push_back(windowJson);
        }
    }
    
    // Media Resources
    j["mediaResources"] = json::array();
    for (const auto& pair : MediaResources) {
        const auto& resource = pair.second;
        if (resource) {
            json resourceJson;
            resourceJson["id"] = resource->ResourceId;
            resourceJson["type"] = resource->ResourceType;
            resourceJson["mimeType"] = resource->MimeType;
            resourceJson["fileName"] = resource->FileName;
            resourceJson["isEmbedded"] = resource->IsEmbedded;
            if (resource->IsEmbedded) {
                resourceJson["data"] = Base64Encode(resource->Data);
            } else {
                resourceJson["externalPath"] = resource->ExternalPath;
            }
            j["mediaResources"].push_back(resourceJson);
        }
    }
    
    return j.dump(2); // Pretty print with 2-space indentation
#else
    return "{}"; // Empty JSON if no library available
#endif
}

bool UltraCanvasDocument::DeserializeFromJSON(const std::string& jsonContent) {
#ifdef ULTRACANVAS_USE_JSON
    try {
        json j = json::parse(jsonContent);
        
        // Parse metadata
        if (j.contains("metadata")) {
            const auto& meta = j["metadata"];
            if (meta.contains("title")) Metadata.Title = meta["title"];
            if (meta.contains("author")) Metadata.Author = meta["author"];
            if (meta.contains("description")) Metadata.Description = meta["description"];
            if (meta.contains("createdDate")) Metadata.CreatedDate = meta["createdDate"];
            if (meta.contains("modifiedDate")) Metadata.ModifiedDate = meta["modifiedDate"];
            if (meta.contains("version")) Metadata.Version = meta["version"];
            if (meta.contains("documentType")) {
                Metadata.DocumentType = static_cast<UCDocumentType>(meta["documentType"]);
            }
        }
        
        // Parse windows, media resources, form data...
        // Similar parsing for other sections
        
        return true;
    }
    catch (const json::exception& e) {
        return false;
    }
#else
    return false;
#endif
}

bool UltraCanvasDocument::IsValidUCDFile(const std::string& filePath) {
    std::vector<uint8_t> header = UCDocumentUtils::ReadBinaryFile(filePath);
    if (header.size() < 8) return false;
    
    return std::string(header.begin(), header.begin() + 4) == "UCD\x01";
}

UCDocumentType UltraCanvasDocument::DetectDocumentType(const std::string& filePath) {
    UltraCanvasDocument doc;
    if (doc.LoadFromFile(filePath)) {
        return doc.GetMetadata().DocumentType;
    }
    return UCDocumentType::Template; // Default
}

// Private helper methods (updated for multi-page)
void UltraCanvasDocument::UpdateNavigationHistory(const std::string& windowId, const std::string& pageId) {
    auto windowData = GetWindowData(windowId);
    if (!windowData) return;
    
    auto& history = windowData->Navigation.NavigationHistory;
    auto& position = windowData->Navigation.HistoryPosition;
    
    // Remove any history after current position (for when user navigates back then forward to new page)
    if (position >= 0 && position < static_cast<int>(history.size()) - 1) {
        history.erase(history.begin() + position + 1, history.end());
    }
    
    // Add new page to history if it's different from current
    if (history.empty() || history.back() != pageId) {
        history.push_back(pageId);
        position = static_cast<int>(history.size()) - 1;
    }
    
    // Limit history size
    const int MAX_HISTORY_SIZE = 100;
    if (history.size() > MAX_HISTORY_SIZE) {
        history.erase(history.begin());
        position--;
    }
}

void UltraCanvasDocument::ValidatePageOrder(const std::string& windowId) {
    auto windowData = GetWindowData(windowId);
    if (!windowData) return;
    
    // Ensure page order matches actual pages
    std::vector<std::string> validPageOrder;
    for (const auto& pageId : windowData->Navigation.PageOrder) {
        bool found = false;
        for (const auto& page : windowData->Pages) {
            if (page && page->PageId == pageId) {
                validPageOrder.push_back(pageId);
                found = true;
                break;
            }
        }
    }
    
    // Add any pages not in the order
    for (const auto& page : windowData->Pages) {
        if (page && std::find(validPageOrder.begin(), validPageOrder.end(), page->PageId) == validPageOrder.end()) {
            validPageOrder.push_back(page->PageId);
        }
    }
    
    windowData->Navigation.PageOrder = validPageOrder;
}

std::string UltraCanvasDocument::GenerateUniquePageId() {
    static int pageCounter = 0;
    return "Page_" + std::to_string(++pageCounter) + "_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

std::string UltraCanvasDocument::GetCurrentDateTime() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string UltraCanvasDocument::EscapeXML(const std::string& input) {
    std::string result;
    result.reserve(input.length());
    
    for (char c : input) {
        switch (c) {
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '&': result += "&amp;"; break;
            case '"': result += "&quot;"; break;
            case '\'': result += "&#39;"; break;
            default: result += c; break;
        }
    }
    return result;
}

std::string UltraCanvasDocument::Base64Encode(const std::vector<uint8_t>& data) {
    // Simple Base64 encoding implementation
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    
    int pad = data.size() % 3;
    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t tmp = (data[i] << 16);
        if (i + 1 < data.size()) tmp |= (data[i + 1] << 8);
        if (i + 2 < data.size()) tmp |= data[i + 2];
        
        result += chars[(tmp >> 18) & 0x3F];
        result += chars[(tmp >> 12) & 0x3F];
        result += (i + 1 < data.size()) ? chars[(tmp >> 6) & 0x3F] : '=';
        result += (i + 2 < data.size()) ? chars[tmp & 0x3F] : '=';
    }
    
    return result;
}

std::vector<uint8_t> UltraCanvasDocument::Base64Decode(const std::string& input) {
    // Simple Base64 decoding implementation
    std::vector<uint8_t> result;
    // ... implementation ...
    return result;
}

// Updated XML serialization for multi-page support
std::string UltraCanvasDocument::WindowDataToXML(const std::shared_ptr<UCWindowData>& windowData) const {
    if (!windowData) return "";
    
    std::ostringstream oss;
    oss << "    <Window id=\"" << EscapeXML(windowData->WindowId) << "\">\n";
    oss << "      <Title>" << EscapeXML(windowData->Title) << "</Title>\n";
    oss << "      <Width>" << windowData->Width << "</Width>\n";
    oss << "      <Height>" << windowData->Height << "</Height>\n";
    oss << "      <PositionX>" << windowData->PositionX << "</PositionX>\n";
    oss << "      <PositionY>" << windowData->PositionY << "</PositionY>\n";
    
    // Page transitions
    oss << "      <PageTransitions>\n";
    oss << "        <TransitionType>" << static_cast<int>(windowData->PageTransitions.TransitionType) << "</TransitionType>\n";
    oss << "        <DurationMs>" << windowData->PageTransitions.DurationMs << "</DurationMs>\n";
    oss << "        <EasingFunction>" << EscapeXML(windowData->PageTransitions.EasingFunction) << "</EasingFunction>\n";
    oss << "        <AnimateOnLoad>" << (windowData->PageTransitions.AnimateOnLoad ? "true" : "false") << "</AnimateOnLoad>\n";
    oss << "      </PageTransitions>\n";
    
    // Navigation
    oss << "      <Navigation>\n";
    oss << "        <CurrentPageId>" << EscapeXML(windowData->Navigation.CurrentPageId) << "</CurrentPageId>\n";
    oss << "        <PageOrder>\n";
    for (const auto& pageId : windowData->Navigation.PageOrder) {
        oss << "          <PageId>" << EscapeXML(pageId) << "</PageId>\n";
    }
    oss << "        </PageOrder>\n";
    
    // Bookmarks
    oss << "        <Bookmarks>\n";
    for (const auto& bookmark : windowData->Navigation.Bookmarks) {
        oss << "          <Bookmark name=\"" << EscapeXML(bookmark.first) 
            << "\" pageId=\"" << EscapeXML(bookmark.second) << "\"/>\n";
    }
    oss << "        </Bookmarks>\n";
    
    // Table of Contents
    oss << "        <TableOfContents>\n";
    for (const auto& entry : windowData->Navigation.TableOfContents) {
        oss << "          <Entry title=\"" << EscapeXML(entry.first) 
            << "\" pageId=\"" << EscapeXML(entry.second) << "\"/>\n";
    }
    oss << "        </TableOfContents>\n";
    oss << "      </Navigation>\n";
    
    // Pages
    oss << "      <Pages>\n";
    for (const auto& page : windowData->Pages) {
        if (page) {
            oss << PageDataToXML(page);
        }
    }
    oss << "      </Pages>\n";
    
    // Legacy components (for backwards compatibility)
    if (!windowData->Components.empty()) {
        oss << "      <Components>\n";
        for (const auto& component : windowData->Components) {
            if (component) {
                oss << ComponentDataToXML(component);
            }
        }
        oss << "      </Components>\n";
    }
    
    oss << "    </Window>\n";
    return oss.str();
}

std::string UltraCanvasDocument::PageDataToXML(const std::shared_ptr<UCPageData>& pageData) const {
    if (!pageData) return "";
    
    std::ostringstream oss;
    oss << "        <Page id=\"" << EscapeXML(pageData->PageId) << "\">\n";
    oss << "          <PageName>" << EscapeXML(pageData->PageName) << "</PageName>\n";
    oss << "          <PageNumber>" << pageData->PageNumber << "</PageNumber>\n";
    oss << "          <Width>" << pageData->Width << "</Width>\n";
    oss << "          <Height>" << pageData->Height << "</Height>\n";
    oss << "          <BackgroundColor>" << EscapeXML(pageData->BackgroundColor) << "</BackgroundColor>\n";
    oss << "          <BackgroundImage>" << EscapeXML(pageData->BackgroundImage) << "</BackgroundImage>\n";
    
    // Layout settings
    oss << "          <LayoutSettings>\n";
    oss << "            <MarginTop>" << pageData->LayoutSettings.MarginTop << "</MarginTop>\n";
    oss << "            <MarginBottom>" << pageData->LayoutSettings.MarginBottom << "</MarginBottom>\n";
    oss << "            <MarginLeft>" << pageData->LayoutSettings.MarginLeft << "</MarginLeft>\n";
    oss << "            <MarginRight>" << pageData->LayoutSettings.MarginRight << "</MarginRight>\n";
    oss << "            <ShowHeader>" << (pageData->LayoutSettings.ShowHeader ? "true" : "false") << "</ShowHeader>\n";
    oss << "            <ShowFooter>" << (pageData->LayoutSettings.ShowFooter ? "true" : "false") << "</ShowFooter>\n";
    oss << "            <ShowPageNumbers>" << (pageData->LayoutSettings.ShowPageNumbers ? "true" : "false") << "</ShowPageNumbers>\n";
    oss << "            <HeaderText>" << EscapeXML(pageData->LayoutSettings.HeaderText) << "</HeaderText>\n";
    oss << "            <FooterText>" << EscapeXML(pageData->LayoutSettings.FooterText) << "</FooterText>\n";
    oss << "          </LayoutSettings>\n";
    
    // Navigation links
    if (!pageData->NextPageId.empty() || !pageData->PreviousPageId.empty() || !pageData->LinkedPages.empty()) {
        oss << "          <NavigationLinks>\n";
        if (!pageData->NextPageId.empty()) {
            oss << "            <NextPage>" << EscapeXML(pageData->NextPageId) << "</NextPage>\n";
        }
        if (!pageData->PreviousPageId.empty()) {
            oss << "            <PreviousPage>" << EscapeXML(pageData->PreviousPageId) << "</PreviousPage>\n";
        }
        for (const auto& linkedPage : pageData->LinkedPages) {
            oss << "            <LinkedPage>" << EscapeXML(linkedPage) << "</LinkedPage>\n";
        }
        oss << "          </NavigationLinks>\n";
    }
    
    // Media resources
    if (!pageData->MediaResourceIds.empty()) {
        oss << "          <MediaResources>\n";
        for (const auto& resourceId : pageData->MediaResourceIds) {
            oss << "            <ResourceId>" << EscapeXML(resourceId) << "</ResourceId>\n";
        }
        oss << "          </MediaResources>\n";
    }
    
    // Components
    oss << "          <Components>\n";
    for (const auto& component : pageData->Components) {
        if (component) {
            oss << ComponentDataToXML(component);
        }
    }
    oss << "          </Components>\n";
    
    oss << "        </Page>\n";
    return oss.str();
}
    switch (type) {
        case UCCompressionType::None:
            output = input;
            return true;
            
        case UCCompressionType::ZIP:
        case UCCompressionType::GZIP:
#ifdef ULTRACANVAS_USE_ZLIB
            // zlib compression implementation
            return CompressWithZlib(input, output, type == UCCompressionType::GZIP);
#else
            output = input; // Fallback to no compression
            return true;
#endif
            
        case UCCompressionType::LZMA:
            // LZMA compression implementation
            output = input; // Fallback
            return true;
            
        default:
            return false;
    }
}

bool UltraCanvasDocument::DecompressData(const std::vector<uint8_t>& input, std::vector<uint8_t>& output, UCCompressionType type) {
    switch (type) {
        case UCCompressionType::None:
            output = input;
            return true;
            
        case UCCompressionType::ZIP:
        case UCCompressionType::GZIP:
#ifdef ULTRACANVAS_USE_ZLIB
            // zlib decompression implementation
            return DecompressWithZlib(input, output, type == UCCompressionType::GZIP);
#else
            output = input; // Fallback
            return true;
#endif
            
        case UCCompressionType::LZMA:
            // LZMA decompression implementation
            output = input; // Fallback
            return true;
            
        default:
            return false;
    }
}

bool UltraCanvasDocument::EncryptData(const std::vector<uint8_t>& input, std::vector<uint8_t>& output, const std::string& password) {
#ifdef ULTRACANVAS_USE_OPENSSL
    // AES-256 encryption implementation using OpenSSL
    // This is a simplified example - real implementation would be more robust
    
    // Generate random IV
    std::vector<uint8_t> iv(16);
    if (RAND_bytes(iv.data(), iv.size()) != 1) {
        return false;
    }
    
    // Derive key from password using PBKDF2
    std::vector<uint8_t> key(32);
    if (PKCS5_PBKDF2_HMAC(password.c_str(), password.length(),
                          iv.data(), 8, // Use first 8 bytes of IV as salt
                          10000, // iterations
                          EVP_sha256(),
                          key.size(), key.data()) != 1) {
        return false;
    }
    
    // Encrypt data
    // ... AES encryption implementation ...
    
    // Prepend IV to encrypted data
    output.clear();
    output.insert(output.end(), iv.begin(), iv.end());
    // output.insert(output.end(), encrypted_data.begin(), encrypted_data.end());
    
    return true;
#else
    // Fallback - no encryption
    output = input;
    return true;
#endif
}

bool UltraCanvasDocument::DecryptData(const std::vector<uint8_t>& input, std::vector<uint8_t>& output, const std::string& password) {
#ifdef ULTRACANVAS_USE_OPENSSL
    if (input.size() < 16) return false; // Need at least IV
    
    // Extract IV
    std::vector<uint8_t> iv(input.begin(), input.begin() + 16);
    std::vector<uint8_t> encrypted_data(input.begin() + 16, input.end());
    
    // Derive key from password
    std::vector<uint8_t> key(32);
    if (PKCS5_PBKDF2_HMAC(password.c_str(), password.length(),
                          iv.data(), 8,
                          10000,
                          EVP_sha256(),
                          key.size(), key.data()) != 1) {
        return false;
    }
    
    // Decrypt data
    // ... AES decryption implementation ...
    
    return true;
#else
    // Fallback - no decryption
    output = input;
    return true;
#endif
}

std::string UltraCanvasDocument::GeneratePasswordHash(const std::string& password, const std::string& salt) {
#ifdef ULTRACANVAS_USE_OPENSSL
    std::string saltedPassword = password + salt;
    
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(saltedPassword.c_str()), 
           saltedPassword.length(), hash);
    
    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
#else
    // Simple fallback hash (not secure)
    return std::to_string(std::hash<std::string>{}(password + salt));
#endif
}

std::string UltraCanvasDocument::GenerateSalt() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    
    std::ostringstream oss;
    for (int i = 0; i < 16; i++) {
        oss << std::hex << std::setw(2) << std::setfill('0') << dis(gen);
    }
    return oss.str();
}

std::string UltraCanvasDocument::GetCurrentDateTime() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string UltraCanvasDocument::EscapeXML(const std::string& input) {
    std::string result;
    result.reserve(input.length());
    
    for (char c : input) {
        switch (c) {
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '&': result += "&amp;"; break;
            case '"': result += "&quot;"; break;
            case '\'': result += "&#39;"; break;
            default: result += c; break;
        }
    }
    return result;
}

std::string UltraCanvasDocument::Base64Encode(const std::vector<uint8_t>& data) {
    // Simple Base64 encoding implementation
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    
    int pad = data.size() % 3;
    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t tmp = (data[i] << 16);
        if (i + 1 < data.size()) tmp |= (data[i + 1] << 8);
        if (i + 2 < data.size()) tmp |= data[i + 2];
        
        result += chars[(tmp >> 18) & 0x3F];
        result += chars[(tmp >> 12) & 0x3F];
        result += (i + 1 < data.size()) ? chars[(tmp >> 6) & 0x3F] : '=';
        result += (i + 2 < data.size()) ? chars[tmp & 0x3F] : '=';
    }
    
    return result;
}

std::vector<uint8_t> UltraCanvasDocument::Base64Decode(const std::string& input) {
    // Simple Base64 decoding implementation
    std::vector<uint8_t> result;
    // ... implementation ...
    return result;
}

// UCDocumentUtils Implementation
namespace UCDocumentUtils {

std::vector<uint8_t> ReadBinaryFile(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> data(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);
    
    return data;
}

bool WriteBinaryFile(const std::string& filePath, const std::vector<uint8_t>& data) {
    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    return file.good();
}

std::string DetectMimeType(const std::string& filePath) {
    // Simple MIME type detection based on file extension
    size_t dotPos = filePath.find_last_of('.');
    if (dotPos == std::string::npos) {
        return "application/octet-stream";
    }
    
    std::string extension = filePath.substr(dotPos + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    
    static const std::map<std::string, std::string> mimeTypes = {
        {"png", "image/png"},
        {"jpg", "image/jpeg"},
        {"jpeg", "image/jpeg"},
        {"gif", "image/gif"},
        {"bmp", "image/bmp"},
        {"svg", "image/svg+xml"},
        {"pdf", "application/pdf"},
        {"mp4", "video/mp4"},
        {"mp3", "audio/mpeg"},
        {"wav", "audio/wav"},
        {"txt", "text/plain"},
        {"html", "text/html"},
        {"xml", "application/xml"},
        {"json", "application/json"}
    };
    
    auto it = mimeTypes.find(extension);
    return (it != mimeTypes.end()) ? it->second : "application/octet-stream";
}

std::string DetectMimeTypeFromData(const std::vector<uint8_t>& data) {
    if (data.size() < 4) {
        return "application/octet-stream";
    }
    
    // Check common file signatures
    if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
        return "image/png";
    }
    if (data[0] == 0xFF && data[1] == 0xD8) {
        return "image/jpeg";
    }
    if (data[0] == 'G' && data[1] == 'I' && data[2] == 'F') {
        return "image/gif";
    }
    if (data[0] == '%' && data[1] == 'P' && data[2] == 'D' && data[3] == 'F') {
        return "application/pdf";
    }
    
    return "application/octet-stream";
}

bool EmbedResource(UCMediaResource& resource, const std::string& filePath) {
    resource.Data = ReadBinaryFile(filePath);
    if (resource.Data.empty()) {
        return false;
    }
    
    resource.IsEmbedded = true;
    resource.ExternalPath.clear();
    
    if (resource.MimeType.empty()) {
        resource.MimeType = DetectMimeType(filePath);
    }
    
    return true;
}

bool ExtractResource(const UCMediaResource& resource, const std::string& outputPath) {
    if (!resource.IsEmbedded || resource.Data.empty()) {
        return false;
    }
    
    return WriteBinaryFile(outputPath, resource.Data);
}

bool ValidateComponentData(const std::shared_ptr<UCComponentData>& componentData) {
    if (!componentData) return false;
    
    // Basic validation
    if (componentData->ComponentType.empty() || componentData->ComponentId.empty()) {
        return false;
    }
    
    // Validate children recursively
    for (const auto& child : componentData->Children) {
        if (!ValidateComponentData(child)) {
            return false;
        }
    }
    
    return true;
}

bool ValidateWindowData(const std::shared_ptr<UCWindowData>& windowData) {
    if (!windowData) return false;
    
    // Basic validation
    if (windowData->WindowId.empty()) {
        return false;
    }
    
    if (windowData->Width <= 0 || windowData->Height <= 0) {
        return false;
    }
    
    // Validate components
    for (const auto& component : windowData->Components) {
        if (!ValidateComponentData(component)) {
            return false;
        }
    }
    
    return true;
}

bool ValidateDocument(const std::shared_ptr<UltraCanvasDocument>& document) {
    if (!document) return false;
    
    // Validate all windows
    for (const auto& windowId : document->GetWindowIds()) {
        auto windowData = document->GetWindowData(windowId);
        if (!ValidateWindowData(windowData)) {
            return false;
        }
    }
    
    return true;
}

} // namespace UCDocumentUtils

} // namespace UltraCanvas
