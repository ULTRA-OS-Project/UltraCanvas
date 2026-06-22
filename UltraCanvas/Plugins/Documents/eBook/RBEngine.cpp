// Plugins/Documents/eBook/RBEngine.cpp
// RocketEdition (NuvoMedia/Gemstar) RB eBook Engine Implementation
// Version: 1.0.0
// Last Modified: 2025-01-10
// Author: UltraCanvas Framework

#include "RBEngine.h"
#include "EBookDecompression.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <regex>
#include <iomanip>

// For DEFLATE decompression (would use zlib or separate module)
// #include "UltraCanvasCompression.h"

namespace UltraCanvas {

// ============================================================================
// BYTE ORDER HELPERS (RB uses Little-Endian / Intel / Vax order)
// ============================================================================

uint16_t RBEngine::ReadLE16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
           (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t RBEngine::ReadLE32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

// ============================================================================
// RB TOC ENTRY HELPERS
// ============================================================================

std::string RBTOCEntry::GetExtension() const {
    size_t dotPos = name.find_last_of('.');
    if (dotPos == std::string::npos) {
        return "";
    }
    return name.substr(dotPos);
}

std::string RBTOCEntry::GetBaseName() const {
    size_t dotPos = name.find_last_of('.');
    if (dotPos == std::string::npos) {
        return name;
    }
    return name.substr(0, dotPos);
}

// ============================================================================
// CONSTRUCTOR & DESTRUCTOR
// ============================================================================

RBEngine::RBEngine()
    : isLoaded(false)
    , pageCount(0) {
}

RBEngine::~RBEngine() {
    Close();
}

// ============================================================================
// FORMAT IDENTIFICATION
// ============================================================================

EBookFormat RBEngine::GetFormat() const {
    return EBookFormat::RB;
}

std::string RBEngine::GetFormatName() const {
    return "RocketEdition (NuvoMedia)";
}

std::vector<std::string> RBEngine::GetSupportedExtensions() const {
    return { "rb" };
}

// ============================================================================
// DOCUMENT LOADING
// ============================================================================

bool RBEngine::LoadFile(const std::string& filePath, const std::string& password) {
    Close();
    currentFilePath = filePath;
    
    ReportProgress(0.0f, "Opening file...");
    
    // Read file
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        lastError = "Failed to open file: " + filePath;
        return false;
    }
    
    size_t fileSize = file.tellg();
    if (fileSize < RB_HEADER_SIZE) {
        lastError = "File too small to be valid RB";
        return false;
    }
    
    file.seekg(0);
    std::vector<uint8_t> data(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);
    file.close();
    
    return LoadFromMemory(data, password);
}

bool RBEngine::LoadFromMemory(const std::vector<uint8_t>& data,
                               const std::string& password) {
    Close();
    
    if (data.size() < RB_HEADER_SIZE) {
        lastError = "Data too small to be valid RB";
        return false;
    }
    
    ReportProgress(0.1f, "Parsing RB structure...");
    
    // Parse RB file
    if (!ParseRB(data.data(), data.size())) {
        return false;
    }
    
    ReportProgress(0.8f, "Building page structure...");
    
    // Build page structure
    BuildPageStructure();
    
    // Build table of contents
    BuildTableOfContents();
    
    isLoaded = true;
    
    ReportProgress(1.0f, "Complete");
    
    return true;
}

void RBEngine::Close() {
    parsedData = RBParsedData();
    metadata = EBookMetadata();
    tableOfContents.clear();
    pageInfos.clear();
    isLoaded = false;
    pageCount = 0;
    lastError.clear();
}

bool RBEngine::IsLoaded() const {
    return isLoaded;
}

// ============================================================================
// VALIDATION
// ============================================================================

bool RBEngine::ValidateFile(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    // Read magic
    uint8_t magic[RB_MAGIC_LENGTH];
    file.read(reinterpret_cast<char*>(magic), RB_MAGIC_LENGTH);
    if (!file.good()) {
        return false;
    }
    
    return RBDetection::HasRBMagic(magic, RB_MAGIC_LENGTH);
}

// ============================================================================
// STATIC DETECTION METHODS
// ============================================================================

bool RBEngine::IsRBFile(const std::string& filePath) {
    return RBDetection::HasRBExtension(filePath);
}

bool RBEngine::IsRBData(const uint8_t* data, size_t size) {
    return RBDetection::HasRBMagic(data, size);
}

// ============================================================================
// RB PARSING
// ============================================================================

bool RBEngine::ParseRB(const uint8_t* data, size_t size) {
    ReportProgress(0.2f, "Validating header...");
    
    // Validate header
    if (!ValidateHeader(data, size)) {
        return false;
    }
    
    ReportProgress(0.3f, "Parsing header...");
    
    // Parse header
    if (!ParseHeader(data, size)) {
        return false;
    }
    
    ReportProgress(0.4f, "Parsing table of contents...");
    
    // Parse TOC
    if (!ParseTOC(data, size)) {
        return false;
    }
    
    ReportProgress(0.5f, "Extracting content pages...");
    
    // Parse content pages
    if (!ParseContentPages(data, size)) {
        return false;
    }
    
    ReportProgress(0.7f, "Building metadata...");
    
    // Build metadata from info page
    metadata.title = parsedData.info.title;
    metadata.author = parsedData.info.author;
    metadata.publisher = parsedData.info.publisher;
    metadata.description = parsedData.info.comment;
    metadata.language = parsedData.info.language;
    
    // Use filename if no title
    if (metadata.title.empty() && !currentFilePath.empty()) {
        size_t lastSlash = currentFilePath.find_last_of("/\\");
        size_t lastDot = currentFilePath.find_last_of('.');
        
        if (lastSlash != std::string::npos) {
            metadata.title = currentFilePath.substr(lastSlash + 1, lastDot - lastSlash - 1);
        } else if (lastDot != std::string::npos) {
            metadata.title = currentFilePath.substr(0, lastDot);
        }
    }
    
    return true;
}

bool RBEngine::ValidateHeader(const uint8_t* data, size_t size) {
    if (size < RB_MAGIC_LENGTH) {
        lastError = "File too small for RB header";
        return false;
    }
    
    // Check magic
    if (!RBDetection::HasRBMagic(data, size)) {
        lastError = "Invalid RB magic signature";
        return false;
    }
    
    // Check for system files (not eBooks)
    auto rbType = RBDetection::GetRBType(data, size);
    if (rbType == RBDetection::RBType::Code || 
        rbType == RBDetection::RBType::Food) {
        lastError = "File is a system RB file, not an eBook";
        return false;
    }
    
    parsedData.hasValidHeader = true;
    return true;
}

bool RBEngine::ParseHeader(const uint8_t* data, size_t size) {
    if (size < sizeof(RBHeader)) {
        lastError = "File too small for complete header";
        return false;
    }
    
    // Parse header fields
    parsedData.header.magic = ReadLE32(data);
    parsedData.header.versionMajor = ReadLE16(data + 4);
    parsedData.header.versionMinor = data[6];  // Actually just 1 byte used
    std::memcpy(parsedData.header.creator, data + 8, 4);
    parsedData.header.reserved1 = ReadLE32(data + 12);
    parsedData.header.year = ReadLE16(data + 16);
    parsedData.header.month = data[18];
    parsedData.header.day = data[19];
    parsedData.header.tocOffset = ReadLE32(data + 24);
    parsedData.header.fileLength = ReadLE32(data + 28);
    
    parsedData.version = parsedData.header.versionMajor;
    
    // Format creation date
    // Handle both old (full year) and new (tm_year) formats
    uint16_t year = parsedData.header.year;
    if (year < 200) {
        year += 1900;  // tm_year format (years since 1900)
    }
    
    std::ostringstream dateStr;
    dateStr << year << "-" 
            << std::setfill('0') << std::setw(2) << static_cast<int>(parsedData.header.month) << "-"
            << std::setfill('0') << std::setw(2) << static_cast<int>(parsedData.header.day);
    parsedData.creationDate = dateStr.str();
    
    // Validate file length
    if (parsedData.header.fileLength > 0 && parsedData.header.fileLength != size) {
        // File might be truncated, but try to continue
    }
    
    // Check for DRM (non-zero bytes in 0x20-0x127)
    bool hasDRM = false;
    for (size_t i = 0x20; i < std::min(size, size_t(RB_HEADER_SIZE)); ++i) {
        if (data[i] != 0) {
            hasDRM = true;
            break;
        }
    }
    
    if (hasDRM) {
        parsedData.hasEncryptedContent = true;
        // Continue parsing, but encrypted pages won't be readable
    }
    
    return true;
}

bool RBEngine::ParseTOC(const uint8_t* data, size_t size) {
    uint32_t tocOffset = parsedData.header.tocOffset;
    
    if (tocOffset == 0) {
        tocOffset = RB_DEFAULT_TOC_OFFSET;
    }
    
    if (tocOffset + 4 > size) {
        lastError = "TOC offset beyond file";
        return false;
    }
    
    // Read entry count
    uint32_t entryCount = ReadLE32(data + tocOffset);
    
    if (entryCount > 10000) {
        lastError = "Invalid TOC entry count";
        return false;
    }
    
    parsedData.toc.clear();
    parsedData.toc.reserve(entryCount);
    
    size_t offset = tocOffset + 4;
    
    for (uint32_t i = 0; i < entryCount && offset + RB_TOC_ENTRY_SIZE <= size; ++i) {
        RBTOCEntry entry;
        
        // Read name (32 bytes, zero-padded)
        const char* namePtr = reinterpret_cast<const char*>(data + offset);
        size_t nameLen = strnlen(namePtr, RB_TOC_NAME_SIZE);
        entry.name = std::string(namePtr, nameLen);
        
        // Read length, offset, flags
        entry.length = ReadLE32(data + offset + 32);
        entry.offset = ReadLE32(data + offset + 36);
        entry.flags = ReadLE32(data + offset + 40);
        
        // Validate
        if (entry.offset < size) {
            parsedData.toc.push_back(entry);
        }
        
        offset += RB_TOC_ENTRY_SIZE;
    }
    
    return !parsedData.toc.empty();
}

bool RBEngine::ParseContentPages(const uint8_t* data, size_t size) {
    std::ostringstream fullText;
    
    for (const auto& entry : parsedData.toc) {
        RBContentPage page;
        
        if (ExtractPage(data, size, entry, page)) {
            page.name = entry.name;
            page.mimeType = GetMimeType(entry.name);
            page.wasCompressed = entry.IsDeflated();
            page.wasEncrypted = entry.IsEncrypted();
            
            // Parse special pages
            if (entry.IsInfo()) {
                std::string content(page.data.begin(), page.data.end());
                ParseInfoPage(content);
            } else if (entry.GetExtension() == ".hidx") {
                std::string content(page.data.begin(), page.data.end());
                ParseHtmlIndex(entry.GetBaseName(), content);
            } else if (entry.GetExtension() == ".hkey") {
                std::string content(page.data.begin(), page.data.end());
                ParseKeywordIndex(entry.GetBaseName(), content);
            } else if (page.mimeType == "text/html") {
                // Extract text for search
                std::string content(page.data.begin(), page.data.end());
                fullText << ExtractTextFromHTML(content) << "\n";
            }
            
            parsedData.pages[entry.name] = std::move(page);
        }
    }
    
    parsedData.fullTextContent = fullText.str();
    
    return !parsedData.pages.empty();
}

bool RBEngine::ExtractPage(const uint8_t* data, size_t size,
                            const RBTOCEntry& entry, RBContentPage& page) {
    if (entry.offset + entry.length > size) {
        return false;
    }
    
    // Check for encryption
    if (entry.IsEncrypted()) {
        // Cannot extract encrypted content
        return false;
    }
    
    const uint8_t* pageData = data + entry.offset;
    size_t pageSize = entry.length;
    
    // Check for deflate compression
    if (entry.IsDeflated()) {
        page.data = DecompressDeflate(pageData, pageSize);
        if (page.data.empty()) {
            // Decompression failed, store raw
            page.data.assign(pageData, pageData + pageSize);
        }
    } else {
        page.data.assign(pageData, pageData + pageSize);
    }
    
    return !page.data.empty();
}

std::vector<uint8_t> RBEngine::DecompressDeflate(const uint8_t* data, size_t size) {
    // Use EBookDecompression helper for RB chunked DEFLATE
    std::vector<uint8_t> input(data, data + size);
    std::vector<uint8_t> output;
    
    if (EBookDecompression::DecompressRBChunked(input, output)) {
        return output;
    }
    
    // Fallback: return raw data
    return std::vector<uint8_t>(data, data + size);
}

bool RBEngine::ParseInfoPage(const std::string& content) {
    // Parse NAME=VALUE pairs
    std::istringstream stream(content);
    std::string line;
    
    while (std::getline(stream, line)) {
        // Remove trailing \r if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        size_t equalsPos = line.find('=');
        if (equalsPos == std::string::npos) {
            continue;
        }
        
        std::string name = line.substr(0, equalsPos);
        std::string value = line.substr(equalsPos + 1);
        
        // Process known fields
        if (name == "TITLE") {
            parsedData.info.title = value;
        } else if (name == "AUTHOR") {
            parsedData.info.author = value;
        } else if (name == "COMMENT") {
            parsedData.info.comment = value;
        } else if (name == "URL") {
            parsedData.info.url = value;
        } else if (name == "GENERATOR") {
            parsedData.info.generator = value;
        } else if (name == "BODY") {
            parsedData.info.bodyPage = value;
        } else if (name == "MENUMARK") {
            parsedData.info.menuMark = value;
        } else if (name == "ISBN") {
            parsedData.info.isbn = value;
        } else if (name == "PUB_NAME") {
            parsedData.info.publisher = value;
        } else if (name == "COPYRIGHT") {
            parsedData.info.copyright = value;
        } else if (name == "COPYTITLE") {
            // Alternate copyright
        } else if (name == "GENRE") {
            parsedData.info.genre = value;
        } else if (name == "TITLE_LANGUAGE") {
            parsedData.info.language = value;
        } else if (name == "REVISION") {
            parsedData.info.revision = value;
        } else if (name == "TYPE") {
            parsedData.info.type = std::stoi(value);
        } else if (name == "HKEY") {
            parsedData.info.hasHKey = (value == "1");
        }
    }
    
    return true;
}

bool RBEngine::ParseHtmlIndex(const std::string& name, const std::string& content) {
    RBHtmlIndex index;
    std::istringstream stream(content);
    std::string line;
    
    enum class Section { None, Tags, Paragraphs, Names };
    Section currentSection = Section::None;
    
    while (std::getline(stream, line)) {
        // Remove trailing \r
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        // Check for section headers
        if (line.find("[tags ") == 0) {
            currentSection = Section::Tags;
            continue;
        } else if (line.find("[paragraphs ") == 0) {
            currentSection = Section::Paragraphs;
            continue;
        } else if (line.find("[names ") == 0) {
            currentSection = Section::Names;
            continue;
        }
        
        if (line.empty()) {
            continue;
        }
        
        switch (currentSection) {
            case Section::Tags: {
                // Format: <TAG> parentIndex
                size_t lastSpace = line.find_last_of(' ');
                if (lastSpace != std::string::npos) {
                    RBHtmlIndex::TagDef tag;
                    tag.tag = line.substr(0, lastSpace);
                    tag.parentIndex = std::stoi(line.substr(lastSpace + 1));
                    index.tags.push_back(tag);
                }
                break;
            }
            case Section::Paragraphs: {
                // Format: offset tagIndex
                std::istringstream lineStream(line);
                RBHtmlIndex::ParagraphEntry para;
                lineStream >> para.offset >> para.tagIndex;
                index.paragraphs.push_back(para);
                break;
            }
            case Section::Names: {
                // Format: "name" offset
                std::regex nameRegex(R"(\"([^\"]+)\"\s+(\d+))");
                std::smatch match;
                if (std::regex_search(line, match, nameRegex)) {
                    RBHtmlIndex::AnchorEntry anchor;
                    anchor.name = match[1].str();
                    anchor.offset = std::stoul(match[2].str());
                    index.anchors.push_back(anchor);
                }
                break;
            }
            default:
                break;
        }
    }
    
    parsedData.htmlIndices[name] = std::move(index);
    return true;
}

bool RBEngine::ParseKeywordIndex(const std::string& name, const std::string& content) {
    std::vector<RBKeywordEntry> keywords;
    std::istringstream stream(content);
    std::string line;
    
    while (std::getline(stream, line)) {
        // Remove trailing \r
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        // Format: word\toffset
        size_t tabPos = line.find('\t');
        if (tabPos != std::string::npos) {
            RBKeywordEntry entry;
            entry.word = line.substr(0, tabPos);
            entry.offset = std::stoul(line.substr(tabPos + 1));
            keywords.push_back(entry);
        }
    }
    
    parsedData.keywords[name] = std::move(keywords);
    return true;
}

std::string RBEngine::ExtractTextFromHTML(const std::string& html) {
    std::string result;
    
    // Remove scripts and styles
    std::regex scriptRegex(R"(<script[^>]*>[\s\S]*?</script>)", std::regex::icase);
    std::regex styleRegex(R"(<style[^>]*>[\s\S]*?</style>)", std::regex::icase);
    std::string cleaned = std::regex_replace(html, scriptRegex, "");
    cleaned = std::regex_replace(cleaned, styleRegex, "");
    
    // Convert block tags to newlines
    std::regex blockRegex(R"(<(p|div|br|h[1-6]|li|tr)[^>]*>)", std::regex::icase);
    cleaned = std::regex_replace(cleaned, blockRegex, "\n");
    
    // Remove all remaining tags
    std::regex tagRegex(R"(<[^>]+>)");
    cleaned = std::regex_replace(cleaned, tagRegex, "");
    
    // Decode common HTML entities
    std::regex nbspRegex(R"(&nbsp;)");
    std::regex ltRegex(R"(&lt;)");
    std::regex gtRegex(R"(&gt;)");
    std::regex ampRegex(R"(&amp;)");
    
    cleaned = std::regex_replace(cleaned, nbspRegex, " ");
    cleaned = std::regex_replace(cleaned, ltRegex, "<");
    cleaned = std::regex_replace(cleaned, gtRegex, ">");
    cleaned = std::regex_replace(cleaned, ampRegex, "&");
    
    // Collapse whitespace
    std::regex whitespaceRegex(R"(\s+)");
    result = std::regex_replace(cleaned, whitespaceRegex, " ");
    
    return result;
}

std::string RBEngine::GetMimeType(const std::string& name) const {
    size_t dotPos = name.find_last_of('.');
    if (dotPos == std::string::npos) {
        return "application/octet-stream";
    }
    
    std::string ext = name.substr(dotPos);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    if (ext == ".html" || ext == ".htm") return "text/html";
    if (ext == ".hidx") return "text/plain";
    if (ext == ".hkey") return "text/plain";
    if (ext == ".info") return "text/plain";
    if (ext == ".png") return "image/png";
    if (ext == ".gif") return "image/gif";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".css") return "text/css";
    
    return "application/octet-stream";
}

// ============================================================================
// PAGE STRUCTURE
// ============================================================================

void RBEngine::BuildPageStructure() {
    // Estimate pages from HTML content
    const int CHARS_PER_PAGE = 2000;
    
    size_t contentLength = parsedData.fullTextContent.length();
    if (contentLength == 0) {
        contentLength = 1;
    }
    
    pageCount = std::max(1, static_cast<int>((contentLength + CHARS_PER_PAGE - 1) / CHARS_PER_PAGE));
    
    // Build page infos
    pageInfos.clear();
    pageInfos.reserve(pageCount);
    
    size_t offset = 0;
    for (int i = 0; i < pageCount; ++i) {
        EBookPageInfo info;
        info.pageNumber = i + 1;
        info.contentOffset = static_cast<int>(offset);
        info.contentLength = std::min(CHARS_PER_PAGE, 
                                       static_cast<int>(contentLength - offset));
        pageInfos.push_back(info);
        offset += CHARS_PER_PAGE;
    }
}

void RBEngine::BuildTableOfContents() {
    tableOfContents.clear();
    
    // Build from HTML pages
    int pageNum = 1;
    for (const auto& entry : parsedData.toc) {
        if (entry.GetExtension() == ".html" || entry.GetExtension() == ".htm") {
            EBookTOCEntry tocEntry;
            tocEntry.title = entry.GetBaseName();
            tocEntry.level = 0;
            tocEntry.pageNumber = pageNum++;
            tableOfContents.push_back(tocEntry);
        }
    }
    
    // If no entries, create basic entry
    if (tableOfContents.empty() && pageCount > 0) {
        EBookTOCEntry entry;
        entry.title = metadata.title.empty() ? "Document" : metadata.title;
        entry.level = 0;
        entry.pageNumber = 1;
        tableOfContents.push_back(entry);
    }
}

// ============================================================================
// CONTENT ACCESS
// ============================================================================

EBookMetadata RBEngine::GetMetadata() const {
    return metadata;
}

int RBEngine::GetPageCount() const {
    return pageCount;
}

EBookPageInfo RBEngine::GetPageInfo(int pageNumber) const {
    if (pageNumber < 1 || pageNumber > static_cast<int>(pageInfos.size())) {
        return EBookPageInfo();
    }
    return pageInfos[pageNumber - 1];
}

std::vector<EBookTOCEntry> RBEngine::GetTableOfContents() const {
    return tableOfContents;
}

std::string RBEngine::GetPageContent(int pageNumber) const {
    if (pageNumber < 1 || pageNumber > pageCount) {
        return std::string();
    }
    
    const auto& info = pageInfos[pageNumber - 1];
    if (info.contentOffset >= static_cast<int>(parsedData.fullTextContent.length())) {
        return std::string();
    }
    
    return parsedData.fullTextContent.substr(info.contentOffset, info.contentLength);
}

std::vector<uint8_t> RBEngine::RenderPageToImage(int pageNumber,
                                                  int width, int height,
                                                  float dpi) const {
    // Rendering would use text layout engine
    return std::vector<uint8_t>();
}

std::vector<uint8_t> RBEngine::GetCoverImage() const {
    // RB format doesn't have dedicated covers
    // Return first PNG image if available
    for (const auto& [name, page] : parsedData.pages) {
        if (page.mimeType == "image/png" && !page.data.empty()) {
            return page.data;
        }
    }
    return std::vector<uint8_t>();
}

const RBParsedData& RBEngine::GetParsedData() const {
    return parsedData;
}

const RBInfoPage& RBEngine::GetInfoPage() const {
    return parsedData.info;
}

const RBContentPage* RBEngine::GetContentPage(const std::string& name) const {
    auto it = parsedData.pages.find(name);
    if (it != parsedData.pages.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<std::string> RBEngine::GetPageNames() const {
    std::vector<std::string> names;
    names.reserve(parsedData.pages.size());
    
    for (const auto& [name, page] : parsedData.pages) {
        names.push_back(name);
    }
    
    return names;
}

std::vector<std::string> RBEngine::GetHTMLPageNames() const {
    std::vector<std::string> names;
    
    for (const auto& [name, page] : parsedData.pages) {
        if (page.mimeType == "text/html") {
            names.push_back(name);
        }
    }
    
    return names;
}

std::string RBEngine::GetCreationDate() const {
    return parsedData.creationDate;
}

bool RBEngine::HasEncryptedContent() const {
    return parsedData.hasEncryptedContent;
}

// ============================================================================
// SEARCH
// ============================================================================

std::vector<EBookSearchResult> RBEngine::Search(const std::string& query) const {
    std::vector<EBookSearchResult> results;
    
    if (query.empty() || parsedData.fullTextContent.empty()) {
        return results;
    }
    
    // Simple case-insensitive search
    std::string lowerContent = parsedData.fullTextContent;
    std::string lowerQuery = query;
    std::transform(lowerContent.begin(), lowerContent.end(), 
                   lowerContent.begin(), ::tolower);
    std::transform(lowerQuery.begin(), lowerQuery.end(),
                   lowerQuery.begin(), ::tolower);
    
    size_t pos = 0;
    while ((pos = lowerContent.find(lowerQuery, pos)) != std::string::npos) {
        EBookSearchResult result;
        result.matchedText = parsedData.fullTextContent.substr(pos, query.length());
        
        // Extract context
        size_t contextStart = (pos > 50) ? pos - 50 : 0;
        size_t contextEnd = std::min(pos + query.length() + 50, 
                                     parsedData.fullTextContent.length());
        
        result.contextBefore = parsedData.fullTextContent.substr(contextStart, pos - contextStart);
        result.contextAfter = parsedData.fullTextContent.substr(
            pos + query.length(), 
            contextEnd - pos - query.length());
        
        // Determine page number
        for (size_t i = 0; i < pageInfos.size(); ++i) {
            if (pos >= static_cast<size_t>(pageInfos[i].contentOffset) &&
                pos < static_cast<size_t>(pageInfos[i].contentOffset + pageInfos[i].contentLength)) {
                result.pageNumber = static_cast<int>(i) + 1;
                break;
            }
        }
        
        results.push_back(result);
        pos += query.length();
    }
    
    return results;
}

// ============================================================================
// ERROR HANDLING
// ============================================================================

std::string RBEngine::GetLastError() const {
    return lastError;
}

void RBEngine::SetProgressCallback(std::function<void(float, const std::string&)> callback) {
    progressCallback = callback;
}

void RBEngine::ReportProgress(float progress, const std::string& status) {
    if (progressCallback) {
        progressCallback(progress, status);
    }
}

// ============================================================================
// RB DETECTION UTILITIES
// ============================================================================

namespace RBDetection {

bool HasRBExtension(const std::string& filePath) {
    size_t dotPos = filePath.find_last_of('.');
    if (dotPos == std::string::npos) {
        return false;
    }
    
    std::string ext = filePath.substr(dotPos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    return ext == "rb";
}

bool HasRBMagic(const uint8_t* data, size_t size) {
    if (size < 4) {
        return false;
    }
    
    // Check first two bytes: B0 0C
    if (data[0] != 0xB0 || data[1] != 0x0C) {
        return false;
    }
    
    // Check for valid second pair
    // B0 0C = book, C0 DE = code, F0 0D = food
    if ((data[2] == 0xB0 && data[3] == 0x0C) ||  // book
        (data[2] == 0xC0 && data[3] == 0xDE) ||  // code
        (data[2] == 0xF0 && data[3] == 0x0D)) {  // food
        return true;
    }
    
    return false;
}

RBType GetRBType(const uint8_t* data, size_t size) {
    if (size < 4) {
        return RBType::Unknown;
    }
    
    if (data[0] != 0xB0 || data[1] != 0x0C) {
        return RBType::Unknown;
    }
    
    if (data[2] == 0xB0 && data[3] == 0x0C) {
        return RBType::Book;
    } else if (data[2] == 0xC0 && data[3] == 0xDE) {
        return RBType::Code;
    } else if (data[2] == 0xF0 && data[3] == 0x0D) {
        return RBType::Food;
    }
    
    return RBType::Unknown;
}

bool HasEncryption(const uint8_t* data, size_t size) {
    if (size < RB_HEADER_SIZE) {
        return false;
    }
    
    // Check for non-zero bytes in DRM region (0x20-0x127)
    for (size_t i = 0x20; i < RB_HEADER_SIZE; ++i) {
        if (data[i] != 0) {
            return true;
        }
    }
    
    return false;
}

} // namespace RBDetection

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

std::unique_ptr<IEBookEngine> CreateRBEngine() {
    return std::make_unique<RBEngine>();
}

void RegisterRBPlugin() {
    // Register with EBookEngineFactory
    // EBookEngineFactory::RegisterEngine("rb", CreateRBEngine);
}

void UnregisterRBPlugin() {
    // Unregister from EBookEngineFactory
    // EBookEngineFactory::UnregisterEngine("rb");
}

} // namespace UltraCanvas
