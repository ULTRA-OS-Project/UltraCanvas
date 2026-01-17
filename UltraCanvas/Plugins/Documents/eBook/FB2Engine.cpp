// Plugins/Documents/eBook/FB2Engine.cpp
// FictionBook 2.0 (FB2) eBook Engine Implementation
// Version: 1.0.0
// Last Modified: 2025-01-08
// Author: UltraCanvas Framework

#include "FB2Engine.h"
#include "UltraCanvasHTMLConverter.h"

#include <algorithm>
#include <sstream>
#include <cmath>
#include <stack>
#include <regex>
#include <iomanip>
#include <cstring>

// Optional: Use pugixml or tinyxml2 for XML parsing
// For this implementation, we use a simple built-in XML parser

namespace UltraCanvas {

// ============================================================================
// SIMPLE XML PARSER FOR FB2
// ============================================================================

namespace {

struct XMLAttribute {
    std::string name;
    std::string value;
};

struct XMLNode {
    std::string name;
    std::string text;
    std::vector<XMLAttribute> attributes;
    std::vector<std::shared_ptr<XMLNode>> children;
    XMLNode* parent = nullptr;
    
    std::string GetAttribute(const std::string& attrName) const {
        for (const auto& attr : attributes) {
            if (attr.name == attrName) return attr.value;
        }
        return "";
    }
    
    bool HasAttribute(const std::string& attrName) const {
        for (const auto& attr : attributes) {
            if (attr.name == attrName) return true;
        }
        return false;
    }
    
    std::shared_ptr<XMLNode> FindChild(const std::string& childName) const {
        for (const auto& child : children) {
            if (child->name == childName) return child;
        }
        return nullptr;
    }
    
    std::vector<std::shared_ptr<XMLNode>> FindChildren(const std::string& childName) const {
        std::vector<std::shared_ptr<XMLNode>> result;
        for (const auto& child : children) {
            if (child->name == childName) {
                result.push_back(child);
            }
        }
        return result;
    }
    
    std::string GetChildText(const std::string& childName) const {
        auto child = FindChild(childName);
        return child ? child->text : "";
    }
    
    std::string GetAllText() const {
        std::string result = text;
        for (const auto& child : children) {
            result += child->GetAllText();
        }
        return result;
    }
};

class SimpleXMLParser {
public:
    std::shared_ptr<XMLNode> Parse(const std::string& xml) {
        input = xml;
        pos = 0;
        errors.clear();
        
        // Skip BOM
        if (input.length() >= 3 && 
            input[0] == '\xEF' && input[1] == '\xBB' && input[2] == '\xBF') {
            pos = 3;
        }
        
        // Skip XML declaration
        SkipWhitespace();
        if (Match("<?xml")) {
            while (!IsAtEnd() && !Match("?>")) Advance();
            if (Match("?>")) pos += 2;
        }
        
        SkipWhitespace();
        
        // Parse root element
        return ParseElement();
    }
    
    const std::vector<std::string>& GetErrors() const { return errors; }
    bool HasErrors() const { return !errors.empty(); }
    
private:
    std::string input;
    size_t pos = 0;
    std::vector<std::string> errors;
    
    char Current() const { return pos < input.length() ? input[pos] : '\0'; }
    char Peek(int offset = 1) const { 
        size_t p = pos + offset;
        return p < input.length() ? input[p] : '\0';
    }
    void Advance() { if (pos < input.length()) pos++; }
    bool IsAtEnd() const { return pos >= input.length(); }
    
    bool Match(const std::string& str) {
        if (pos + str.length() > input.length()) return false;
        for (size_t i = 0; i < str.length(); ++i) {
            if (input[pos + i] != str[i]) return false;
        }
        return true;
    }
    
    void SkipWhitespace() {
        while (!IsAtEnd() && std::isspace(static_cast<unsigned char>(Current()))) {
            Advance();
        }
    }
    
    std::shared_ptr<XMLNode> ParseElement() {
        SkipWhitespace();
        if (Current() != '<') return nullptr;
        
        Advance(); // <
        
        // Check for comment
        if (Current() == '!' && Peek() == '-' && Peek(2) == '-') {
            pos += 3;
            while (!IsAtEnd() && !Match("-->")) Advance();
            if (Match("-->")) pos += 3;
            return ParseElement();
        }
        
        // Check for CDATA
        if (Match("![CDATA[")) {
            pos += 8;
            std::string cdata;
            while (!IsAtEnd() && !Match("]]>")) {
                cdata += Current();
                Advance();
            }
            if (Match("]]>")) pos += 3;
            
            auto node = std::make_shared<XMLNode>();
            node->name = "#cdata";
            node->text = cdata;
            return node;
        }
        
        auto node = std::make_shared<XMLNode>();
        node->name = ParseName();
        
        // Parse attributes
        while (!IsAtEnd()) {
            SkipWhitespace();
            if (Current() == '/' || Current() == '>') break;
            
            std::string attrName = ParseName();
            if (attrName.empty()) break;
            
            SkipWhitespace();
            std::string attrValue;
            if (Current() == '=') {
                Advance();
                SkipWhitespace();
                attrValue = ParseAttributeValue();
            }
            
            node->attributes.push_back({attrName, attrValue});
        }
        
        SkipWhitespace();
        
        // Self-closing tag
        if (Current() == '/') {
            Advance();
            SkipWhitespace();
            if (Current() == '>') Advance();
            return node;
        }
        
        if (Current() == '>') Advance();
        
        // Parse content
        while (!IsAtEnd()) {
            SkipWhitespace();
            
            if (Match("</")) {
                // End tag
                pos += 2;
                std::string endName = ParseName();
                while (!IsAtEnd() && Current() != '>') Advance();
                if (Current() == '>') Advance();
                break;
            }
            
            if (Current() == '<') {
                // Child element
                auto child = ParseElement();
                if (child) {
                    child->parent = node.get();
                    node->children.push_back(child);
                }
            } else {
                // Text content
                std::string text;
                while (!IsAtEnd() && Current() != '<') {
                    if (Current() == '&') {
                        text += ParseEntity();
                    } else {
                        text += Current();
                        Advance();
                    }
                }
                if (!text.empty()) {
                    // Trim and add to node text
                    size_t start = text.find_first_not_of(" \t\n\r");
                    if (start != std::string::npos) {
                        size_t end = text.find_last_not_of(" \t\n\r");
                        node->text += text.substr(start, end - start + 1);
                    }
                }
            }
        }
        
        return node;
    }
    
    std::string ParseName() {
        std::string name;
        while (!IsAtEnd() && (std::isalnum(Current()) || Current() == '-' || 
               Current() == '_' || Current() == ':' || Current() == '.')) {
            name += Current();
            Advance();
        }
        return name;
    }
    
    std::string ParseAttributeValue() {
        if (Current() == '"' || Current() == '\'') {
            char quote = Current();
            Advance();
            std::string value;
            while (!IsAtEnd() && Current() != quote) {
                if (Current() == '&') {
                    value += ParseEntity();
                } else {
                    value += Current();
                    Advance();
                }
            }
            if (Current() == quote) Advance();
            return value;
        }
        
        // Unquoted value
        std::string value;
        while (!IsAtEnd() && !std::isspace(Current()) && 
               Current() != '>' && Current() != '/') {
            value += Current();
            Advance();
        }
        return value;
    }
    
    std::string ParseEntity() {
        if (Current() != '&') return "";
        Advance();
        
        std::string entity;
        while (!IsAtEnd() && Current() != ';' && entity.length() < 10) {
            entity += Current();
            Advance();
        }
        if (Current() == ';') Advance();
        
        if (entity == "amp") return "&";
        if (entity == "lt") return "<";
        if (entity == "gt") return ">";
        if (entity == "quot") return "\"";
        if (entity == "apos") return "'";
        if (entity == "nbsp") return " ";
        
        // Numeric entity
        if (!entity.empty() && entity[0] == '#') {
            try {
                int code = 0;
                if (entity.length() > 1 && (entity[1] == 'x' || entity[1] == 'X')) {
                    code = std::stoi(entity.substr(2), nullptr, 16);
                } else {
                    code = std::stoi(entity.substr(1));
                }
                if (code < 128) return std::string(1, static_cast<char>(code));
                // UTF-8 encoding for higher codes
                if (code < 0x800) {
                    char buf[3] = {
                        static_cast<char>(0xC0 | (code >> 6)),
                        static_cast<char>(0x80 | (code & 0x3F)),
                        0
                    };
                    return buf;
                }
            } catch (...) {}
        }
        
        return "&" + entity + ";";
    }
};

// Base64 decoder
std::vector<uint8_t> DecodeBase64(const std::string& encoded) {
    static const std::string chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    std::vector<uint8_t> result;
    result.reserve(encoded.length() * 3 / 4);
    
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) {
        T[static_cast<unsigned char>(chars[i])] = i;
    }
    
    int val = 0;
    int valb = -8;
    
    for (unsigned char c : encoded) {
        if (std::isspace(c)) continue;
        if (c == '=') break;
        if (T[c] == -1) continue;
        
        val = (val << 6) + T[c];
        valb += 6;
        
        if (valb >= 0) {
            result.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    
    return result;
}

} // anonymous namespace

// ============================================================================
// FB2 AUTHOR IMPLEMENTATION
// ============================================================================

std::string FB2Author::GetFullName() const {
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

// ============================================================================
// FB2 SECTION IMPLEMENTATION
// ============================================================================

int FB2Section::GetTotalSections() const {
    int count = 1;
    for (const auto& sub : sections) {
        count += sub.GetTotalSections();
    }
    return count;
}

// ============================================================================
// FB2 DOCUMENT IMPLEMENTATION
// ============================================================================

FB2Body* FB2Document::GetMainBody() {
    for (auto& body : bodies) {
        if (body.IsMainBody()) return &body;
    }
    return bodies.empty() ? nullptr : &bodies[0];
}

const FB2Body* FB2Document::GetMainBody() const {
    for (const auto& body : bodies) {
        if (body.IsMainBody()) return &body;
    }
    return bodies.empty() ? nullptr : &bodies[0];
}

FB2Body* FB2Document::GetNotesBody() {
    for (auto& body : bodies) {
        if (body.IsNotes()) return &body;
    }
    return nullptr;
}

const FB2Body* FB2Document::GetNotesBody() const {
    for (const auto& body : bodies) {
        if (body.IsNotes()) return &body;
    }
    return nullptr;
}

bool FB2Document::HasCover() const {
    return !titleInfo.coverPageImage.empty();
}

// ============================================================================
// FB2 ENGINE IMPLEMENTATION
// ============================================================================

FB2Engine::FB2Engine() 
    : pageCount(0)
    , isLoaded(false) {
}

FB2Engine::~FB2Engine() {
    CloseDocument();
}

EBookFormat FB2Engine::GetFormat() const {
    return EBookFormat::FB2;
}

bool FB2Engine::LoadDocument(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(renderMutex);
    
    CloseDocument();
    
    // Read file
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        lastError = "Failed to open file: " + filePath;
        return false;
    }
    
    size_t fileSize = file.tellg();
    file.seekg(0);
    
    std::vector<uint8_t> data(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);
    file.close();
    
    // Check if it's a ZIP file (fb2.zip)
    if (fileSize > 4 && data[0] == 'P' && data[1] == 'K' && 
        data[2] == 0x03 && data[3] == 0x04) {
        // TODO: Decompress ZIP and extract FB2
        // For now, we don't support compressed FB2
        lastError = "Compressed FB2 (fb2.zip) not yet supported";
        return false;
    }
    
    return LoadDocumentFromMemory(data.data(), data.size());
}

bool FB2Engine::LoadDocumentFromMemory(const uint8_t* data, size_t size) {
    std::lock_guard<std::mutex> lock(renderMutex);
    
    CloseDocument();
    
    // Convert to string
    std::string xml(reinterpret_cast<const char*>(data), size);
    
    // Parse FB2
    if (!ParseFB2(xml)) {
        return false;
    }
    
    // Build metadata
    BuildMetadata();
    
    // Build table of contents
    BuildTableOfContents();
    
    // Flatten sections for page access
    FlattenSections();
    
    // Calculate pages
    CalculatePages(lastRenderSettings);
    
    isLoaded = true;
    return true;
}

void FB2Engine::CloseDocument() {
    document = FB2Document();
    metadata = EBookMetadata();
    tableOfContents.clear();
    pageInfos.clear();
    flattenedSections.clear();
    pageMapping.clear();
    pageCount = 0;
    isLoaded = false;
    lastError.clear();
}

bool FB2Engine::IsLoaded() const {
    return isLoaded;
}

std::string FB2Engine::GetLastError() const {
    return lastError;
}

EBookMetadata FB2Engine::GetMetadata() const {
    return metadata;
}

int FB2Engine::GetPageCount() const {
    return pageCount;
}

int FB2Engine::GetChapterCount() const {
    const FB2Body* mainBody = document.GetMainBody();
    if (!mainBody) return 0;
    return static_cast<int>(mainBody->sections.size());
}

std::vector<EBookTOCEntry> FB2Engine::GetTableOfContents() const {
    return tableOfContents;
}

EBookPageInfo FB2Engine::GetPageInfo(int pageNumber) const {
    if (pageNumber < 0 || pageNumber >= static_cast<int>(pageInfos.size())) {
        return EBookPageInfo();
    }
    return pageInfos[pageNumber];
}

std::vector<uint8_t> FB2Engine::RenderPage(int pageNumber, const EBookRenderSettings& settings) {
    std::lock_guard<std::mutex> lock(renderMutex);
    
    if (!isLoaded || pageNumber < 0 || pageNumber >= pageCount) {
        return {};
    }
    
    // Recalculate pages if settings changed
    if (settings.pageWidth != lastRenderSettings.pageWidth ||
        settings.pageHeight != lastRenderSettings.pageHeight ||
        settings.fontSize != lastRenderSettings.fontSize) {
        lastRenderSettings = settings;
        CalculatePages(settings);
    }
    
    // Get section content for this page
    std::string html = GetPageHTML(pageNumber, settings);
    
    // Convert to image using HTMLConverter
    HTMLConversionOptions options = HTMLConversionOptions::EBookMode();
    options.viewportWidth = static_cast<float>(settings.pageWidth);
    options.viewportHeight = static_cast<float>(settings.pageHeight);
    options.defaultFontSize = settings.fontSize;
    options.lineHeight = settings.lineSpacing;
    
    // Apply reading mode colors
    switch (settings.readingMode) {
        case EBookReadingMode::Sepia:
            options.defaultBackgroundColor = Color(250, 240, 220, 255);
            options.defaultTextColor = Color(90, 70, 50, 255);
            break;
        case EBookReadingMode::Night:
            options.defaultBackgroundColor = Color(30, 30, 30, 255);
            options.defaultTextColor = Color(180, 180, 180, 255);
            break;
        case EBookReadingMode::HighContrast:
            options.defaultBackgroundColor = Color(0, 0, 0, 255);
            options.defaultTextColor = Color(255, 255, 255, 255);
            break;
        default:
            options.defaultBackgroundColor = Color(255, 255, 255, 255);
            options.defaultTextColor = Color(0, 0, 0, 255);
            break;
    }
    
    // Image loader for embedded images
    options.imageLoader = [this](const std::string& src, std::vector<uint8_t>& data, 
                                  int& w, int& h) -> bool {
        // Remove # prefix if present
        std::string imageId = src;
        if (!imageId.empty() && imageId[0] == '#') {
            imageId = imageId.substr(1);
        }
        
        // Find binary
        for (const auto& binary : document.binaries) {
            if (binary.id == imageId) {
                data = binary.data;
                w = binary.width;
                h = binary.height;
                return !data.empty();
            }
        }
        return false;
    };
    
    auto converter = std::make_shared<HTMLConverter>();
    return converter->RenderToImage(html, settings.pageWidth, settings.pageHeight, options);
}

std::vector<uint8_t> FB2Engine::RenderPageThumbnail(int pageNumber, int width, int height) {
    EBookRenderSettings settings;
    settings.pageWidth = width;
    settings.pageHeight = height;
    settings.fontSize = 8.0f;  // Smaller for thumbnail
    
    return RenderPage(pageNumber, settings);
}

std::pair<int, int> FB2Engine::GetRenderedPageSize(const EBookRenderSettings& settings) const {
    return {settings.pageWidth, settings.pageHeight};
}

void FB2Engine::PreloadPage(int pageNumber) {
    // No-op for FB2 - pages are generated on demand
}

std::string FB2Engine::ExtractPageText(int pageNumber) const {
    if (!isLoaded || pageNumber < 0 || pageNumber >= pageCount) {
        return "";
    }
    
    // Find section for this page
    auto it = pageMapping.find(pageNumber);
    if (it == pageMapping.end()) return "";
    
    const PageMappingEntry& entry = it->second;
    if (entry.sectionIndex >= flattenedSections.size()) return "";
    
    return flattenedSections[entry.sectionIndex]->plainText;
}

std::string FB2Engine::ExtractAllText() const {
    if (!isLoaded) return "";
    
    std::string text;
    for (const auto* section : flattenedSections) {
        if (!text.empty()) text += "\n\n";
        if (!section->title.empty()) {
            text += section->title + "\n\n";
        }
        text += section->plainText;
    }
    return text;
}

TextPosition FB2Engine::GetTextAtPosition(int pageNumber, int x, int y) const {
    // Simplified - would need layout info for accurate positioning
    TextPosition pos;
    pos.pageNumber = pageNumber;
    pos.characterOffset = 0;
    return pos;
}

std::string FB2Engine::GetTextInRect(int pageNumber, int x, int y, int width, int height) const {
    // Simplified - return page text
    return ExtractPageText(pageNumber);
}

std::vector<EBookSearchResult> FB2Engine::SearchText(const std::string& query, 
                                                      const EBookSearchOptions& options) const {
    std::vector<EBookSearchResult> results;
    if (!isLoaded || query.empty()) return results;
    
    std::string searchQuery = query;
    if (!options.caseSensitive) {
        std::transform(searchQuery.begin(), searchQuery.end(), searchQuery.begin(), ::tolower);
    }
    
    for (int page = 0; page < pageCount; ++page) {
        std::string pageText = ExtractPageText(page);
        std::string searchText = pageText;
        
        if (!options.caseSensitive) {
            std::transform(searchText.begin(), searchText.end(), searchText.begin(), ::tolower);
        }
        
        size_t pos = 0;
        while ((pos = searchText.find(searchQuery, pos)) != std::string::npos) {
            EBookSearchResult result;
            result.pageNumber = page;
            result.textOffset = static_cast<int>(pos);
            result.textLength = static_cast<int>(query.length());
            
            // Extract context
            int contextStart = std::max(0, static_cast<int>(pos) - 30);
            int contextEnd = std::min(static_cast<int>(pageText.length()), 
                                      static_cast<int>(pos + query.length() + 30));
            result.context = pageText.substr(contextStart, contextEnd - contextStart);
            
            // Chapter info
            auto it = pageMapping.find(page);
            if (it != pageMapping.end() && it->second.sectionIndex < flattenedSections.size()) {
                result.chapterTitle = flattenedSections[it->second.sectionIndex]->title;
            }
            
            results.push_back(result);
            pos += query.length();
            
            if (options.maxResults > 0 && 
                static_cast<int>(results.size()) >= options.maxResults) {
                return results;
            }
        }
    }
    
    return results;
}

std::vector<EBookSearchResult> FB2Engine::SearchTextInPage(int pageNumber, 
                                                            const std::string& query,
                                                            const EBookSearchOptions& options) const {
    std::vector<EBookSearchResult> results;
    if (!isLoaded || pageNumber < 0 || pageNumber >= pageCount) return results;
    
    std::string pageText = ExtractPageText(pageNumber);
    std::string searchQuery = query;
    std::string searchText = pageText;
    
    if (!options.caseSensitive) {
        std::transform(searchQuery.begin(), searchQuery.end(), searchQuery.begin(), ::tolower);
        std::transform(searchText.begin(), searchText.end(), searchText.begin(), ::tolower);
    }
    
    size_t pos = 0;
    while ((pos = searchText.find(searchQuery, pos)) != std::string::npos) {
        EBookSearchResult result;
        result.pageNumber = pageNumber;
        result.textOffset = static_cast<int>(pos);
        result.textLength = static_cast<int>(query.length());
        
        int contextStart = std::max(0, static_cast<int>(pos) - 30);
        int contextEnd = std::min(static_cast<int>(pageText.length()), 
                                  static_cast<int>(pos + query.length() + 30));
        result.context = pageText.substr(contextStart, contextEnd - contextStart);
        
        results.push_back(result);
        pos += query.length();
    }
    
    return results;
}

int FB2Engine::GetChapterStartPage(int chapterIndex) const {
    if (chapterIndex < 0 || chapterIndex >= static_cast<int>(tableOfContents.size())) {
        return 0;
    }
    return tableOfContents[chapterIndex].pageNumber;
}

int FB2Engine::GetChapterForPage(int pageNumber) const {
    if (!isLoaded || pageNumber < 0) return 0;
    
    int chapter = 0;
    for (size_t i = 0; i < tableOfContents.size(); ++i) {
        if (tableOfContents[i].pageNumber <= pageNumber) {
            chapter = static_cast<int>(i);
        } else {
            break;
        }
    }
    return chapter;
}

std::string FB2Engine::ResolveLink(const std::string& link) const {
    // Internal links in FB2 start with #
    if (!link.empty() && link[0] == '#') {
        std::string id = link.substr(1);
        
        // Find section with this ID
        for (size_t i = 0; i < flattenedSections.size(); ++i) {
            if (flattenedSections[i]->id == id) {
                // Find page for this section
                for (const auto& [page, entry] : pageMapping) {
                    if (entry.sectionIndex == i) {
                        return "page:" + std::to_string(page);
                    }
                }
            }
        }
        
        // Check for note
        const FB2Body* notes = document.GetNotesBody();
        if (notes) {
            for (const auto& section : notes->sections) {
                if (section.id == id) {
                    return "note:" + id;
                }
            }
        }
    }
    
    return link;
}

bool FB2Engine::SupportsReflow() const {
    return true;  // FB2 is inherently reflowable
}

std::string FB2Engine::GetReflowedContent(int pageNumber, int width, int height, 
                                           float fontSize) const {
    EBookRenderSettings settings;
    settings.pageWidth = width;
    settings.pageHeight = height;
    settings.fontSize = fontSize;
    
    // This would need to recalculate pagination
    // For now, return the HTML for the original page
    return GetPageHTML(pageNumber, settings);
}

int FB2Engine::CalculateReflowPages(int width, int height, float fontSize) const {
    // Estimate based on total text length
    std::string allText = ExtractAllText();
    
    float charsPerLine = width / (fontSize * 0.5f);
    float linesPerPage = height / (fontSize * 1.5f);
    float charsPerPage = charsPerLine * linesPerPage;
    
    if (charsPerPage <= 0) return 1;
    
    return std::max(1, static_cast<int>(std::ceil(allText.length() / charsPerPage)));
}

bool FB2Engine::HasCoverImage() const {
    return document.HasCover();
}

std::vector<uint8_t> FB2Engine::GetCoverImage() const {
    if (!document.HasCover()) return {};
    
    std::string coverId = document.titleInfo.coverPageImage;
    if (!coverId.empty() && coverId[0] == '#') {
        coverId = coverId.substr(1);
    }
    
    for (const auto& binary : document.binaries) {
        if (binary.id == coverId) {
            return binary.data;
        }
    }
    
    return {};
}

std::pair<int, int> FB2Engine::GetCoverImageSize() const {
    if (!document.HasCover()) return {0, 0};
    
    std::string coverId = document.titleInfo.coverPageImage;
    if (!coverId.empty() && coverId[0] == '#') {
        coverId = coverId.substr(1);
    }
    
    for (const auto& binary : document.binaries) {
        if (binary.id == coverId) {
            return {binary.width, binary.height};
        }
    }
    
    return {0, 0};
}

std::vector<std::string> FB2Engine::GetEmbeddedImages() const {
    std::vector<std::string> ids;
    for (const auto& binary : document.binaries) {
        if (binary.contentType.find("image") != std::string::npos) {
            ids.push_back(binary.id);
        }
    }
    return ids;
}

std::vector<uint8_t> FB2Engine::GetEmbeddedImage(const std::string& imageId) const {
    for (const auto& binary : document.binaries) {
        if (binary.id == imageId) {
            return binary.data;
        }
    }
    return {};
}

std::vector<std::string> FB2Engine::GetEmbeddedFonts() const {
    // FB2 doesn't typically embed fonts
    return {};
}

std::vector<uint8_t> FB2Engine::GetEmbeddedFont(const std::string& fontId) const {
    return {};
}

std::vector<std::string> FB2Engine::GetStylesheets() const {
    std::vector<std::string> sheets;
    if (!document.stylesheet.empty()) {
        sheets.push_back("stylesheet");
    }
    return sheets;
}

std::string FB2Engine::GetStylesheetContent(const std::string& stylesheetId) const {
    if (stylesheetId == "stylesheet") {
        return document.stylesheet;
    }
    return "";
}

std::string FB2Engine::GetEngineName() const {
    return "FB2Engine";
}

std::string FB2Engine::GetEngineVersion() const {
    return "1.0.0";
}

std::vector<std::string> FB2Engine::GetSupportedExtensions() const {
    return {"fb2", "fb2.zip"};
}

EBookEngineCapabilities FB2Engine::GetCapabilities() const {
    EBookEngineCapabilities caps;
    caps.supportsReflow = true;
    caps.supportsSearch = true;
    caps.supportsAnnotations = true;
    caps.supportsBookmarks = true;
    caps.supportsCoverImage = true;
    caps.supportsTableOfContents = true;
    caps.supportsEmbeddedFonts = false;
    caps.supportsEmbeddedImages = true;
    caps.supportsStylesheets = true;
    caps.supportsDRM = false;
    caps.supportsEncryption = false;
    caps.maxZoom = 400.0f;
    caps.minZoom = 50.0f;
    return caps;
}

bool FB2Engine::ValidateFile(const std::string& filePath) const {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;
    
    // Read first 100 bytes
    char buffer[100];
    file.read(buffer, 100);
    size_t bytesRead = file.gcount();
    file.close();
    
    std::string header(buffer, bytesRead);
    
    // Check for XML declaration or FictionBook tag
    return header.find("<?xml") != std::string::npos ||
           header.find("<FictionBook") != std::string::npos ||
           header.find("<fictionbook") != std::string::npos;
}

bool FB2Engine::IsPasswordProtected(const std::string& filePath) const {
    return false;  // FB2 doesn't support password protection
}

bool FB2Engine::SetPassword(const std::string& password) {
    return true;  // No-op
}

// ============================================================================
// FB2-SPECIFIC METHODS
// ============================================================================

const FB2Document& FB2Engine::GetFB2Document() const {
    return document;
}

std::string FB2Engine::GetSectionContent(const std::string& sectionId) const {
    for (const auto* section : flattenedSections) {
        if (section->id == sectionId) {
            return section->content;
        }
    }
    return "";
}

std::string FB2Engine::GetNote(const std::string& noteId) const {
    const FB2Body* notes = document.GetNotesBody();
    if (!notes) return "";
    
    std::function<std::string(const std::vector<FB2Section>&)> findNote;
    findNote = [&noteId, &findNote](const std::vector<FB2Section>& sections) -> std::string {
        for (const auto& section : sections) {
            if (section.id == noteId) {
                return section.plainText;
            }
            std::string result = findNote(section.sections);
            if (!result.empty()) return result;
        }
        return "";
    };
    
    return findNote(notes->sections);
}

std::string FB2Engine::ConvertToHTML(const std::string& fb2Content) const {
    // Convert FB2 markup to HTML
    std::string html = fb2Content;
    
    // Replace FB2 tags with HTML equivalents
    std::vector<std::pair<std::string, std::string>> replacements = {
        {"<emphasis>", "<em>"},
        {"</emphasis>", "</em>"},
        {"<strong>", "<strong>"},
        {"</strong>", "</strong>"},
        {"<strikethrough>", "<del>"},
        {"</strikethrough>", "</del>"},
        {"<sub>", "<sub>"},
        {"</sub>", "</sub>"},
        {"<sup>", "<sup>"},
        {"</sup>", "</sup>"},
        {"<code>", "<code>"},
        {"</code>", "</code>"},
        {"<p>", "<p>"},
        {"</p>", "</p>"},
        {"<empty-line/>", "<br/><br/>"},
        {"<empty-line />", "<br/><br/>"},
        {"<subtitle>", "<h3>"},
        {"</subtitle>", "</h3>"},
        {"<epigraph>", "<blockquote class=\"epigraph\">"},
        {"</epigraph>", "</blockquote>"},
        {"<cite>", "<blockquote class=\"cite\">"},
        {"</cite>", "</blockquote>"},
        {"<poem>", "<div class=\"poem\">"},
        {"</poem>", "</div>"},
        {"<stanza>", "<div class=\"stanza\">"},
        {"</stanza>", "</div>"},
        {"<v>", "<p class=\"verse\">"},
        {"</v>", "</p>"},
        {"<text-author>", "<p class=\"text-author\">"},
        {"</text-author>", "</p>"},
        {"<date>", "<span class=\"date\">"},
        {"</date>", "</span>"},
    };
    
    for (const auto& [from, to] : replacements) {
        size_t pos = 0;
        while ((pos = html.find(from, pos)) != std::string::npos) {
            html.replace(pos, from.length(), to);
            pos += to.length();
        }
    }
    
    // Handle <a> links
    std::regex linkRegex(R"(<a\s+([^>]*)>)");
    html = std::regex_replace(html, linkRegex, "<a $1>");
    
    // Handle <image> tags
    std::regex imageRegex(R"(<image\s+[^>]*href="([^"]*)"[^>]*/>)");
    html = std::regex_replace(html, imageRegex, "<img src=\"$1\" />");
    
    std::regex imageRegex2(R"(<image\s+[^>]*l:href="([^"]*)"[^>]*/>)");
    html = std::regex_replace(html, imageRegex2, "<img src=\"$1\" />");
    
    return html;
}

// ============================================================================
// PRIVATE METHODS
// ============================================================================

bool FB2Engine::ParseFB2(const std::string& xml) {
    SimpleXMLParser parser;
    auto root = parser.Parse(xml);
    
    if (!root || parser.HasErrors()) {
        lastError = "Failed to parse FB2 XML";
        return false;
    }
    
    if (root->name != "FictionBook" && root->name != "fictionbook") {
        lastError = "Invalid FB2 file: root element must be FictionBook";
        return false;
    }
    
    // Parse description
    auto description = root->FindChild("description");
    if (description) {
        ParseDescription(description.get());
    }
    
    // Parse bodies
    for (const auto& child : root->children) {
        if (child->name == "body") {
            ParseBody(child.get());
        } else if (child->name == "binary") {
            ParseBinary(child.get());
        } else if (child->name == "stylesheet") {
            document.stylesheet = child->text;
        }
    }
    
    return true;
}

void FB2Engine::ParseDescription(XMLNode* descNode) {
    if (!descNode) return;
    
    // Title info
    auto titleInfo = descNode->FindChild("title-info");
    if (titleInfo) {
        ParseTitleInfo(titleInfo.get());
    }
    
    // Source title info (for translations)
    auto srcTitleInfo = descNode->FindChild("src-title-info");
    if (srcTitleInfo) {
        // Parse similar to title-info but store in srcTitleInfo
    }
    
    // Document info
    auto docInfo = descNode->FindChild("document-info");
    if (docInfo) {
        ParseDocumentInfo(docInfo.get());
    }
    
    // Publish info
    auto pubInfo = descNode->FindChild("publish-info");
    if (pubInfo) {
        ParsePublishInfo(pubInfo.get());
    }
    
    // Custom info
    for (const auto& child : descNode->children) {
        if (child->name == "custom-info") {
            FB2CustomInfo info;
            info.infoType = child->GetAttribute("info-type");
            info.value = child->text;
            document.customInfo.push_back(info);
        }
    }
}

void FB2Engine::ParseTitleInfo(XMLNode* node) {
    if (!node) return;
    
    // Genres
    for (const auto& child : node->FindChildren("genre")) {
        document.titleInfo.genres.push_back(child->text);
    }
    
    // Authors
    for (const auto& child : node->FindChildren("author")) {
        document.titleInfo.authors.push_back(ParseAuthor(child.get()));
    }
    
    // Book title
    document.titleInfo.bookTitle = node->GetChildText("book-title");
    
    // Annotation
    auto annotation = node->FindChild("annotation");
    if (annotation) {
        document.titleInfo.annotation = annotation->GetAllText();
    }
    
    // Keywords
    document.titleInfo.keywords = node->GetChildText("keywords");
    
    // Date
    document.titleInfo.date = node->GetChildText("date");
    
    // Cover page
    auto coverpage = node->FindChild("coverpage");
    if (coverpage) {
        auto image = coverpage->FindChild("image");
        if (image) {
            document.titleInfo.coverPageImage = image->GetAttribute("l:href");
            if (document.titleInfo.coverPageImage.empty()) {
                document.titleInfo.coverPageImage = image->GetAttribute("href");
            }
        }
    }
    
    // Language
    document.titleInfo.language = node->GetChildText("lang");
    document.titleInfo.srcLanguage = node->GetChildText("src-lang");
    
    // Translators
    for (const auto& child : node->FindChildren("translator")) {
        document.titleInfo.translators.push_back(ParseAuthor(child.get()));
    }
    
    // Sequence
    auto sequence = node->FindChild("sequence");
    if (sequence) {
        document.titleInfo.sequenceName = sequence->GetAttribute("name");
        std::string numStr = sequence->GetAttribute("number");
        if (!numStr.empty()) {
            try {
                document.titleInfo.sequenceNumber = std::stoi(numStr);
            } catch (...) {}
        }
    }
}

void FB2Engine::ParseDocumentInfo(XMLNode* node) {
    if (!node) return;
    
    for (const auto& child : node->FindChildren("author")) {
        document.documentInfo.authors.push_back(ParseAuthor(child.get()));
    }
    
    document.documentInfo.programUsed = node->GetChildText("program-used");
    document.documentInfo.date = node->GetChildText("date");
    document.documentInfo.srcUrl = node->GetChildText("src-url");
    document.documentInfo.srcOcr = node->GetChildText("src-ocr");
    document.documentInfo.id = node->GetChildText("id");
    document.documentInfo.version = node->GetChildText("version");
    
    auto history = node->FindChild("history");
    if (history) {
        document.documentInfo.history = history->GetAllText();
    }
    
    for (const auto& child : node->FindChildren("publisher")) {
        document.documentInfo.publishers.push_back(child->text);
    }
}

void FB2Engine::ParsePublishInfo(XMLNode* node) {
    if (!node) return;
    
    document.publishInfo.bookName = node->GetChildText("book-name");
    document.publishInfo.publisher = node->GetChildText("publisher");
    document.publishInfo.city = node->GetChildText("city");
    document.publishInfo.year = node->GetChildText("year");
    document.publishInfo.isbn = node->GetChildText("isbn");
    
    auto sequence = node->FindChild("sequence");
    if (sequence) {
        document.publishInfo.sequence = sequence->GetAttribute("name");
        std::string numStr = sequence->GetAttribute("number");
        if (!numStr.empty()) {
            try {
                document.publishInfo.sequenceNumber = std::stoi(numStr);
            } catch (...) {}
        }
    }
}

FB2Author FB2Engine::ParseAuthor(XMLNode* node) {
    FB2Author author;
    if (!node) return author;
    
    author.firstName = node->GetChildText("first-name");
    author.middleName = node->GetChildText("middle-name");
    author.lastName = node->GetChildText("last-name");
    author.nickname = node->GetChildText("nickname");
    author.homePage = node->GetChildText("home-page");
    author.email = node->GetChildText("email");
    author.id = node->GetChildText("id");
    
    return author;
}

void FB2Engine::ParseBody(XMLNode* node) {
    if (!node) return;
    
    FB2Body body;
    body.name = node->GetAttribute("name");
    
    // Title
    auto title = node->FindChild("title");
    if (title) {
        body.title = title->GetAllText();
    }
    
    // Epigraphs
    for (const auto& child : node->FindChildren("epigraph")) {
        body.epigraphs.push_back(child->GetAllText());
    }
    
    // Sections
    for (const auto& child : node->FindChildren("section")) {
        body.sections.push_back(ParseSection(child.get(), 0));
    }
    
    document.bodies.push_back(std::move(body));
}

FB2Section FB2Engine::ParseSection(XMLNode* node, int depth) {
    FB2Section section;
    if (!node) return section;
    
    section.id = node->GetAttribute("id");
    section.depth = depth;
    
    // Title
    auto title = node->FindChild("title");
    if (title) {
        section.title = title->GetAllText();
    }
    
    // Epigraphs
    for (const auto& child : node->FindChildren("epigraph")) {
        section.epigraphs.push_back(child->GetAllText());
    }
    
    // Content and images
    std::ostringstream content;
    for (const auto& child : node->children) {
        if (child->name == "section") {
            section.sections.push_back(ParseSection(child.get(), depth + 1));
        } else if (child->name == "title" || child->name == "epigraph") {
            // Already processed
        } else if (child->name == "image") {
            std::string href = child->GetAttribute("l:href");
            if (href.empty()) href = child->GetAttribute("href");
            section.images.push_back(href);
            content << "<image l:href=\"" << href << "\"/>";
        } else {
            // Add raw content
            content << "<" << child->name;
            for (const auto& attr : child->attributes) {
                content << " " << attr.name << "=\"" << attr.value << "\"";
            }
            if (child->children.empty() && child->text.empty()) {
                content << "/>";
            } else {
                content << ">" << child->text;
                // Recursively add children content
                for (const auto& subChild : child->children) {
                    content << subChild->GetAllText();
                }
                content << "</" << child->name << ">";
            }
        }
    }
    
    section.content = content.str();
    section.plainText = ExtractPlainText(section.content);
    
    return section;
}

void FB2Engine::ParseBinary(XMLNode* node) {
    if (!node) return;
    
    FB2Binary binary;
    binary.id = node->GetAttribute("id");
    binary.contentType = node->GetAttribute("content-type");
    
    // Decode base64 data
    binary.data = DecodeBase64(node->text);
    
    // Try to determine image dimensions
    if (!binary.data.empty()) {
        DecodeImageDimensions(binary);
    }
    
    document.binaries.push_back(std::move(binary));
}

void FB2Engine::DecodeImageDimensions(FB2Binary& binary) {
    if (binary.data.size() < 8) return;
    
    const uint8_t* data = binary.data.data();
    
    // PNG
    if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
        if (binary.data.size() >= 24) {
            binary.width = (data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19];
            binary.height = (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
        }
    }
    // JPEG
    else if (data[0] == 0xFF && data[1] == 0xD8) {
        size_t offset = 2;
        while (offset + 4 < binary.data.size()) {
            if (data[offset] != 0xFF) break;
            uint8_t marker = data[offset + 1];
            uint16_t length = (data[offset + 2] << 8) | data[offset + 3];
            
            if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
                if (offset + 9 < binary.data.size()) {
                    binary.height = (data[offset + 5] << 8) | data[offset + 6];
                    binary.width = (data[offset + 7] << 8) | data[offset + 8];
                }
                break;
            }
            offset += 2 + length;
        }
    }
    // GIF
    else if (data[0] == 'G' && data[1] == 'I' && data[2] == 'F') {
        if (binary.data.size() >= 10) {
            binary.width = data[6] | (data[7] << 8);
            binary.height = data[8] | (data[9] << 8);
        }
    }
}

std::string FB2Engine::ExtractPlainText(const std::string& content) {
    std::string text;
    bool inTag = false;
    
    for (size_t i = 0; i < content.length(); ++i) {
        char c = content[i];
        if (c == '<') {
            inTag = true;
            // Check for paragraph end - add space
            if (i + 1 < content.length() && content[i + 1] == '/') {
                if (!text.empty() && text.back() != ' ' && text.back() != '\n') {
                    text += ' ';
                }
            }
        } else if (c == '>') {
            inTag = false;
        } else if (!inTag) {
            text += c;
        }
    }
    
    // Normalize whitespace
    std::string normalized;
    bool lastWasSpace = true;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!lastWasSpace) {
                normalized += ' ';
                lastWasSpace = true;
            }
        } else {
            normalized += c;
            lastWasSpace = false;
        }
    }
    
    // Trim
    size_t start = normalized.find_first_not_of(' ');
    if (start == std::string::npos) return "";
    size_t end = normalized.find_last_not_of(' ');
    return normalized.substr(start, end - start + 1);
}

void FB2Engine::BuildMetadata() {
    metadata.title = document.titleInfo.bookTitle;
    
    for (const auto& author : document.titleInfo.authors) {
        if (!metadata.author.empty()) metadata.author += ", ";
        metadata.author += author.GetFullName();
    }
    
    metadata.language = document.titleInfo.language;
    metadata.description = document.titleInfo.annotation;
    metadata.publisher = document.publishInfo.publisher;
    metadata.publicationDate = document.publishInfo.year;
    metadata.isbn = document.publishInfo.isbn;
    
    for (const auto& genre : document.titleInfo.genres) {
        metadata.subjects.push_back(genre);
    }
    
    if (!document.titleInfo.sequenceName.empty()) {
        metadata.series = document.titleInfo.sequenceName;
        metadata.seriesIndex = document.titleInfo.sequenceNumber;
    }
    
    metadata.format = EBookFormat::FB2;
}

void FB2Engine::BuildTableOfContents() {
    tableOfContents.clear();
    
    const FB2Body* mainBody = document.GetMainBody();
    if (!mainBody) return;
    
    int pageNumber = 0;
    
    std::function<void(const std::vector<FB2Section>&, int)> buildTOC;
    buildTOC = [this, &pageNumber, &buildTOC](const std::vector<FB2Section>& sections, int level) {
        for (const auto& section : sections) {
            EBookTOCEntry entry;
            entry.title = section.title.empty() ? "(Untitled)" : section.title;
            entry.level = level;
            entry.pageNumber = pageNumber;
            entry.anchor = section.id;
            
            tableOfContents.push_back(entry);
            pageNumber++;
            
            buildTOC(section.sections, level + 1);
        }
    };
    
    buildTOC(mainBody->sections, 0);
}

void FB2Engine::FlattenSections() {
    flattenedSections.clear();
    
    const FB2Body* mainBody = document.GetMainBody();
    if (!mainBody) return;
    
    std::function<void(const std::vector<FB2Section>&)> flatten;
    flatten = [this, &flatten](const std::vector<FB2Section>& sections) {
        for (const auto& section : sections) {
            flattenedSections.push_back(&section);
            flatten(section.sections);
        }
    };
    
    flatten(mainBody->sections);
}

void FB2Engine::CalculatePages(const EBookRenderSettings& settings) {
    pageInfos.clear();
    pageMapping.clear();
    
    if (flattenedSections.empty()) {
        pageCount = 0;
        return;
    }
    
    // Estimate characters per page
    float charsPerLine = settings.pageWidth / (settings.fontSize * 0.5f);
    float linesPerPage = settings.pageHeight / (settings.fontSize * settings.lineSpacing);
    int charsPerPage = static_cast<int>(charsPerLine * linesPerPage);
    if (charsPerPage <= 0) charsPerPage = 1000;
    
    int currentPage = 0;
    
    for (size_t i = 0; i < flattenedSections.size(); ++i) {
        const FB2Section* section = flattenedSections[i];
        int sectionLength = static_cast<int>(section->plainText.length());
        int sectionPages = std::max(1, (sectionLength + charsPerPage - 1) / charsPerPage);
        
        for (int p = 0; p < sectionPages; ++p) {
            EBookPageInfo info;
            info.pageNumber = currentPage;
            info.chapterIndex = static_cast<int>(i);
            info.chapterTitle = section->title;
            info.isFirstPageOfChapter = (p == 0);
            info.isLastPageOfChapter = (p == sectionPages - 1);
            
            pageInfos.push_back(info);
            
            PageMappingEntry entry;
            entry.sectionIndex = i;
            entry.pageInSection = p;
            entry.globalPage = currentPage;
            entry.sectionId = section->id;
            
            pageMapping[currentPage] = entry;
            currentPage++;
        }
    }
    
    pageCount = currentPage;
}

std::string FB2Engine::GetPageHTML(int pageNumber, const EBookRenderSettings& settings) const {
    auto it = pageMapping.find(pageNumber);
    if (it == pageMapping.end()) return "";
    
    const PageMappingEntry& entry = it->second;
    if (entry.sectionIndex >= flattenedSections.size()) return "";
    
    const FB2Section* section = flattenedSections[entry.sectionIndex];
    
    std::ostringstream html;
    html << "<!DOCTYPE html>\n<html><head><style>\n";
    html << "body { font-family: Georgia, serif; font-size: " << settings.fontSize << "px; ";
    html << "line-height: " << settings.lineSpacing << "; margin: " << settings.marginTop << "px ";
    html << settings.marginRight << "px " << settings.marginBottom << "px " << settings.marginLeft << "px; }\n";
    html << "h1, h2, h3 { text-align: center; }\n";
    html << ".epigraph { font-style: italic; margin-left: 20%; }\n";
    html << ".poem { margin-left: 20px; }\n";
    html << ".verse { margin: 0; }\n";
    html << ".text-author { text-align: right; font-style: italic; }\n";
    html << "</style></head><body>\n";
    
    // Add title on first page of section
    if (entry.pageInSection == 0 && !section->title.empty()) {
        html << "<h2>" << section->title << "</h2>\n";
    }
    
    // Add epigraphs on first page
    if (entry.pageInSection == 0) {
        for (const auto& epigraph : section->epigraphs) {
            html << "<blockquote class=\"epigraph\">" << epigraph << "</blockquote>\n";
        }
    }
    
    // Convert FB2 content to HTML
    std::string content = ConvertToHTML(section->content);
    html << content;
    
    html << "</body></html>";
    
    return html.str();
}

// ============================================================================
// PLUGIN REGISTRATION
// ============================================================================

std::unique_ptr<IEBookEngine> CreateFB2Engine() {
    return std::make_unique<FB2Engine>();
}

void RegisterFB2Plugin() {
    // Register with EBookEngineFactory
    // EBookEngineFactory::RegisterEngine("fb2", CreateFB2Engine);
}

void UnregisterFB2Plugin() {
    // Unregister from factory
    // EBookEngineFactory::UnregisterEngine("fb2");
}

} // namespace UltraCanvas
