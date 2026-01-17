// Plugins/Documents/eBook/LRFEngine.cpp
// Sony BBeB (Broad Band eBook) LRF eBook Engine Implementation
// Version: 1.0.0
// Last Modified: 2025-01-10
// Author: UltraCanvas Framework

#include "LRFEngine.h"
#include "EBookDecompression.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <regex>

// For ZLIB decompression (would use zlib library)
// #include <zlib.h>

namespace UltraCanvas {

// ============================================================================
// BYTE ORDER HELPERS (LRF uses Little-Endian)
// ============================================================================

uint16_t LRFEngine::ReadLE16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
           (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t LRFEngine::ReadLE32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

uint64_t LRFEngine::ReadLE64(const uint8_t* data) {
    return static_cast<uint64_t>(data[0]) |
           (static_cast<uint64_t>(data[1]) << 8) |
           (static_cast<uint64_t>(data[2]) << 16) |
           (static_cast<uint64_t>(data[3]) << 24) |
           (static_cast<uint64_t>(data[4]) << 32) |
           (static_cast<uint64_t>(data[5]) << 40) |
           (static_cast<uint64_t>(data[6]) << 48) |
           (static_cast<uint64_t>(data[7]) << 56);
}

// ============================================================================
// CONSTRUCTOR & DESTRUCTOR
// ============================================================================

LRFEngine::LRFEngine()
    : isLoaded(false)
    , pageCount(0) {
}

LRFEngine::~LRFEngine() {
    Close();
}

// ============================================================================
// FORMAT IDENTIFICATION
// ============================================================================

EBookFormat LRFEngine::GetFormat() const {
    return EBookFormat::LRF;
}

std::string LRFEngine::GetFormatName() const {
    return "Sony BBeB (Broad Band eBook)";
}

std::vector<std::string> LRFEngine::GetSupportedExtensions() const {
    return { "lrf", "lrs" };
}

// ============================================================================
// DOCUMENT LOADING
// ============================================================================

bool LRFEngine::LoadFile(const std::string& filePath, const std::string& password) {
    Close();
    currentFilePath = filePath;
    
    ReportProgress(0.0f, "Opening file...");
    
    // Check for encrypted LRX files
    if (LRFDetection::IsLRXFile(filePath)) {
        lastError = "LRX (DRM-encrypted) files are not supported";
        return false;
    }
    
    // Read file
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        lastError = "Failed to open file: " + filePath;
        return false;
    }
    
    size_t fileSize = file.tellg();
    if (fileSize < LRF_HEADER_SIZE) {
        lastError = "File too small to be valid LRF";
        return false;
    }
    
    file.seekg(0);
    std::vector<uint8_t> data(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);
    file.close();
    
    return LoadFromMemory(data, password);
}

bool LRFEngine::LoadFromMemory(const std::vector<uint8_t>& data,
                                const std::string& password) {
    Close();
    
    if (data.size() < LRF_HEADER_SIZE) {
        lastError = "Data too small to be valid LRF";
        return false;
    }
    
    ReportProgress(0.1f, "Parsing LRF structure...");
    
    // Parse LRF file
    if (!ParseLRF(data.data(), data.size())) {
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

void LRFEngine::Close() {
    parsedData = LRFParsedData();
    metadata = EBookMetadata();
    tableOfContents.clear();
    pageInfos.clear();
    isLoaded = false;
    pageCount = 0;
    lastError.clear();
}

bool LRFEngine::IsLoaded() const {
    return isLoaded;
}

// ============================================================================
// VALIDATION
// ============================================================================

bool LRFEngine::ValidateFile(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    // Read magic
    uint8_t magic[LRF_MAGIC_LENGTH];
    file.read(reinterpret_cast<char*>(magic), LRF_MAGIC_LENGTH);
    if (!file.good()) {
        return false;
    }
    
    return LRFDetection::HasLRFMagic(magic, LRF_MAGIC_LENGTH);
}

// ============================================================================
// STATIC DETECTION METHODS
// ============================================================================

bool LRFEngine::IsLRFFile(const std::string& filePath) {
    return LRFDetection::HasLRFExtension(filePath);
}

bool LRFEngine::IsLRFData(const uint8_t* data, size_t size) {
    return LRFDetection::HasLRFMagic(data, size);
}

// ============================================================================
// LRF PARSING
// ============================================================================

bool LRFEngine::ParseLRF(const uint8_t* data, size_t size) {
    ReportProgress(0.2f, "Validating header...");
    
    // Validate and parse header
    if (!ValidateHeader(data, size)) {
        return false;
    }
    
    if (!ParseHeader(data, size)) {
        return false;
    }
    
    ReportProgress(0.3f, "Parsing object index...");
    
    // Parse object index
    if (!ParseObjectIndex(data, size)) {
        return false;
    }
    
    ReportProgress(0.4f, "Parsing objects...");
    
    // Parse objects
    if (!ParseObjects(data, size)) {
        return false;
    }
    
    ReportProgress(0.6f, "Parsing metadata...");
    
    // Parse metadata (if present)
    ParseMetadata(data, size);
    
    // Parse thumbnail (if present)
    ParseThumbnail(data, size);
    
    ReportProgress(0.7f, "Extracting text content...");
    
    // Extract text content
    ExtractTextContent();
    
    return true;
}

bool LRFEngine::ValidateHeader(const uint8_t* data, size_t size) {
    if (size < LRF_HEADER_SIZE) {
        lastError = "File too small for LRF header";
        return false;
    }
    
    // Check magic signature
    if (std::memcmp(data, LRF_MAGIC, LRF_MAGIC_LENGTH) != 0) {
        lastError = "Invalid LRF magic signature";
        return false;
    }
    
    parsedData.hasValidHeader = true;
    return true;
}

bool LRFEngine::ParseHeader(const uint8_t* data, size_t size) {
    // Parse header fields (Little-Endian)
    std::memcpy(parsedData.header.signature, data, 4);
    parsedData.header.version = ReadLE16(data + 4);
    parsedData.header.pseudoEncryptionKey = ReadLE16(data + 6);
    parsedData.header.rootObjectId = ReadLE32(data + 8);
    parsedData.header.numberOfObjects = ReadLE64(data + 12);
    parsedData.header.objectIndexOffset = ReadLE64(data + 20);
    
    // Parse additional fields based on version
    if (size >= 78) {
        parsedData.header.docInfoSize = ReadLE32(data + 40);
        parsedData.header.width = ReadLE16(data + 44);
        parsedData.header.height = ReadLE16(data + 46);
        parsedData.header.colorDepth = data[48];
        
        if (size >= 58) {
            parsedData.header.tocObjectId = ReadLE32(data + 52);
            parsedData.header.tocObjectOffset = ReadLE32(data + 56);
        }
        
        if (size >= 66) {
            parsedData.header.compressedMetadataSize = ReadLE32(data + 60);
            parsedData.header.uncompressedMetadataSize = ReadLE32(data + 64);
        }
    }
    
    parsedData.version = parsedData.header.version;
    
    // Validate object count
    if (parsedData.header.numberOfObjects > 100000) {
        lastError = "Invalid object count in header";
        return false;
    }
    
    // Validate object index offset
    if (parsedData.header.objectIndexOffset >= size) {
        lastError = "Invalid object index offset";
        return false;
    }
    
    return true;
}

bool LRFEngine::ParseObjectIndex(const uint8_t* data, size_t size) {
    size_t indexOffset = static_cast<size_t>(parsedData.header.objectIndexOffset);
    size_t objectCount = static_cast<size_t>(parsedData.header.numberOfObjects);
    
    // Each index entry is 12 bytes (objectId, offset, size)
    size_t indexSize = objectCount * sizeof(LRFObjectIndexEntry);
    
    if (indexOffset + indexSize > size) {
        lastError = "Object index extends beyond file";
        return false;
    }
    
    parsedData.objectIndex.clear();
    parsedData.objectIndex.reserve(objectCount);
    
    const uint8_t* ptr = data + indexOffset;
    for (size_t i = 0; i < objectCount; ++i) {
        LRFObjectIndexEntry entry;
        entry.objectId = ReadLE32(ptr);
        entry.offset = ReadLE32(ptr + 4);
        entry.size = ReadLE32(ptr + 8);
        
        if (entry.offset < size && entry.offset + entry.size <= size) {
            parsedData.objectIndex.push_back(entry);
        }
        
        ptr += 12;
    }
    
    return !parsedData.objectIndex.empty();
}

bool LRFEngine::ParseObjects(const uint8_t* data, size_t size) {
    for (const auto& entry : parsedData.objectIndex) {
        LRFObject object;
        object.id = entry.objectId;
        object.offset = entry.offset;
        object.size = entry.size;
        
        if (ParseObject(data, size, entry.offset, object)) {
            ParseObjectData(object);
            parsedData.objects[object.id] = std::move(object);
        }
    }
    
    return !parsedData.objects.empty();
}

bool LRFEngine::ParseObject(const uint8_t* data, size_t size,
                             uint32_t offset, LRFObject& object) {
    if (offset + 4 > size) {
        return false;
    }
    
    const uint8_t* ptr = data + offset;
    const uint8_t* end = data + std::min(static_cast<size_t>(offset + object.size), size);
    
    // Parse object tags
    while (ptr + 4 <= end) {
        uint16_t tag = ReadLE16(ptr);
        uint16_t tagData = ReadLE16(ptr + 2);
        ptr += 4;
        
        if (tag == LRFTag::ObjectStart) {
            object.type = static_cast<LRFObjectType>(tagData);
        } else if (tag == LRFTag::ObjectEnd) {
            break;
        } else if (tag == LRFTag::StreamStart) {
            // Stream data follows
            // Size is specified by StreamSize tag
        } else if (tag == LRFTag::StreamSize) {
            uint32_t streamSize = ReadLE32(ptr - 2);
            ptr += 2; // Additional 2 bytes for 32-bit size
            
            if (ptr + streamSize <= end) {
                object.data.assign(ptr, ptr + streamSize);
                ptr += streamSize;
            }
        } else if (tag == LRFTag::ContainedObjectsStart) {
            // Parse contained object IDs
            while (ptr + 4 <= end) {
                uint16_t innerTag = ReadLE16(ptr);
                if (innerTag == LRFTag::ContainedObjectsEnd) {
                    ptr += 4;
                    break;
                } else if (innerTag == LRFTag::Link) {
                    uint32_t linkedId = ReadLE32(ptr + 2);
                    object.containedObjectIds.push_back(linkedId);
                    ptr += 6;
                } else {
                    ptr += 4;
                }
            }
        }
    }
    
    return true;
}

void LRFEngine::ParseObjectData(LRFObject& object) {
    // Apply descrambling if needed
    if (parsedData.header.pseudoEncryptionKey != 0 && !object.data.empty()) {
        DescrambleData(object.data);
    }
    
    // Check if data is compressed
    if (!object.data.empty() && LRFDecompressor::IsZlibCompressed(object.data.data(), object.data.size())) {
        object.data = DecompressData(object.data.data(), object.data.size());
        parsedData.isCompressed = true;
    }
    
    // Parse based on type
    switch (object.type) {
        case LRFObjectType::Text:
        case LRFObjectType::SimpleText:
            if (!object.data.empty()) {
                object.textContent = ParseTextStream(object.data.data(), object.data.size());
            }
            break;
            
        case LRFObjectType::ImageStream:
            if (!object.data.empty()) {
                object.imageData = object.data;
                
                // Detect image type
                if (object.data.size() >= 2) {
                    if (object.data[0] == 0xFF && object.data[1] == 0xD8) {
                        object.imageMimeType = "image/jpeg";
                    } else if (object.data.size() >= 8 &&
                               object.data[0] == 0x89 && object.data[1] == 'P' &&
                               object.data[2] == 'N' && object.data[3] == 'G') {
                        object.imageMimeType = "image/png";
                    } else if (object.data.size() >= 6 &&
                               object.data[0] == 'G' && object.data[1] == 'I' && object.data[2] == 'F') {
                        object.imageMimeType = "image/gif";
                    } else if (object.data[0] == 'B' && object.data[1] == 'M') {
                        object.imageMimeType = "image/bmp";
                    }
                }
            }
            break;
            
        default:
            break;
    }
}

std::vector<uint8_t> LRFEngine::DecompressData(const uint8_t* data, size_t size) {
    return LRFDecompressor::DecompressZlib(data, size);
}

void LRFEngine::DescrambleData(std::vector<uint8_t>& data) {
    // XOR descrambling using pseudo-encryption key
    uint16_t key = parsedData.header.pseudoEncryptionKey;
    
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] ^= static_cast<uint8_t>(key);
        key = (key >> 1) | ((key & 1) << 15); // Rotate key
    }
}

bool LRFEngine::ParseMetadata(const uint8_t* data, size_t size) {
    // Metadata is typically stored after the header
    // Format depends on version
    
    if (parsedData.header.docInfoSize > 0) {
        size_t metadataOffset = LRF_HEADER_SIZE;
        size_t metadataSize = parsedData.header.docInfoSize;
        
        if (metadataOffset + metadataSize <= size) {
            // Parse XML-like metadata structure
            std::string metaStr(reinterpret_cast<const char*>(data + metadataOffset), 
                               metadataSize);
            
            // Extract fields using simple parsing
            // (In practice, would use proper XML parser)
            
            // Look for common patterns
            std::regex titleRegex(R"(<dc:title>([^<]+)</dc:title>)", std::regex::icase);
            std::regex authorRegex(R"(<dc:creator>([^<]+)</dc:creator>)", std::regex::icase);
            std::regex publisherRegex(R"(<dc:publisher>([^<]+)</dc:publisher>)", std::regex::icase);
            std::regex languageRegex(R"(<dc:language>([^<]+)</dc:language>)", std::regex::icase);
            
            std::smatch match;
            if (std::regex_search(metaStr, match, titleRegex)) {
                parsedData.metadata.title = match[1].str();
            }
            if (std::regex_search(metaStr, match, authorRegex)) {
                parsedData.metadata.author = match[1].str();
            }
            if (std::regex_search(metaStr, match, publisherRegex)) {
                parsedData.metadata.publisher = match[1].str();
            }
            if (std::regex_search(metaStr, match, languageRegex)) {
                parsedData.metadata.language = match[1].str();
            }
        }
    }
    
    // Build EBookMetadata from LRFMetadata
    metadata.title = parsedData.metadata.title;
    metadata.author = parsedData.metadata.author;
    metadata.publisher = parsedData.metadata.publisher;
    metadata.language = parsedData.metadata.language;
    
    // Use filename as title if not found
    if (metadata.title.empty() && !currentFilePath.empty()) {
        size_t lastSlash = currentFilePath.find_last_of("/\\");
        size_t lastDot = currentFilePath.find_last_of('.');
        
        if (lastSlash != std::string::npos) {
            metadata.title = currentFilePath.substr(lastSlash + 1, lastDot - lastSlash - 1);
        }
    }
    
    return true;
}

bool LRFEngine::ParseThumbnail(const uint8_t* data, size_t size) {
    // Look for thumbnail/cover in ImageStream objects
    for (const auto& [id, object] : parsedData.objects) {
        if (object.type == LRFObjectType::ImageStream && !object.imageData.empty()) {
            // First image is often the cover
            if (parsedData.thumbnailData.empty()) {
                parsedData.thumbnailData = object.imageData;
                parsedData.thumbnailMimeType = object.imageMimeType;
            }
            break;
        }
    }
    
    return !parsedData.thumbnailData.empty();
}

void LRFEngine::ExtractTextContent() {
    std::ostringstream fullText;
    
    // Iterate through all text objects
    for (const auto& [id, object] : parsedData.objects) {
        if ((object.type == LRFObjectType::Text || 
             object.type == LRFObjectType::SimpleText) &&
            !object.textContent.empty()) {
            fullText << object.textContent << "\n";
        }
    }
    
    parsedData.fullTextContent = fullText.str();
}

std::string LRFEngine::ParseTextStream(const uint8_t* data, size_t size) {
    return LRFTextParser::ParseStream(data, size);
}

std::string LRFEngine::ConvertUTF16LEToUTF8(const uint8_t* data, size_t size) {
    return LRFTextParser::ConvertText(data, size);
}

// ============================================================================
// PAGE STRUCTURE
// ============================================================================

void LRFEngine::BuildPageStructure() {
    parsedData.pages.clear();
    
    // Find PageTree or Page objects
    for (const auto& [id, object] : parsedData.objects) {
        if (object.type == LRFObjectType::Page) {
            LRFPageObject page;
            page.objectId = id;
            page.blockIds = object.containedObjectIds;
            
            // Extract page content from blocks
            std::ostringstream pageContent;
            for (uint32_t blockId : object.containedObjectIds) {
                auto it = parsedData.objects.find(blockId);
                if (it != parsedData.objects.end() && !it->second.textContent.empty()) {
                    pageContent << it->second.textContent;
                }
            }
            page.content = pageContent.str();
            
            parsedData.pages.push_back(std::move(page));
        }
    }
    
    // If no page structure found, create synthetic pages
    if (parsedData.pages.empty() && !parsedData.fullTextContent.empty()) {
        const int CHARS_PER_PAGE = 2000;
        size_t contentLength = parsedData.fullTextContent.length();
        size_t numPages = (contentLength + CHARS_PER_PAGE - 1) / CHARS_PER_PAGE;
        
        for (size_t i = 0; i < numPages; ++i) {
            LRFPageObject page;
            page.objectId = static_cast<uint32_t>(i + 1);
            size_t start = i * CHARS_PER_PAGE;
            size_t len = std::min(static_cast<size_t>(CHARS_PER_PAGE), contentLength - start);
            page.content = parsedData.fullTextContent.substr(start, len);
            parsedData.pages.push_back(std::move(page));
        }
    }
    
    pageCount = static_cast<int>(parsedData.pages.size());
    
    // Build page infos
    pageInfos.clear();
    pageInfos.reserve(pageCount);
    
    int offset = 0;
    for (int i = 0; i < pageCount; ++i) {
        EBookPageInfo info;
        info.pageNumber = i + 1;
        info.contentOffset = offset;
        info.contentLength = static_cast<int>(parsedData.pages[i].content.length());
        pageInfos.push_back(info);
        offset += info.contentLength;
    }
}

void LRFEngine::BuildTableOfContents() {
    tableOfContents.clear();
    
    // Look for TOC object
    if (parsedData.header.tocObjectId != 0) {
        auto it = parsedData.objects.find(parsedData.header.tocObjectId);
        if (it != parsedData.objects.end()) {
            const auto& tocObject = it->second;
            
            // Parse TOC entries from contained objects
            for (uint32_t entryId : tocObject.containedObjectIds) {
                auto entryIt = parsedData.objects.find(entryId);
                if (entryIt != parsedData.objects.end()) {
                    EBookTOCEntry entry;
                    entry.title = entryIt->second.textContent;
                    entry.level = 0;
                    entry.pageNumber = static_cast<int>(tableOfContents.size()) + 1;
                    
                    if (!entry.title.empty()) {
                        tableOfContents.push_back(entry);
                    }
                }
            }
        }
    }
    
    // If no TOC found, create basic entry
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

EBookMetadata LRFEngine::GetMetadata() const {
    return metadata;
}

int LRFEngine::GetPageCount() const {
    return pageCount;
}

EBookPageInfo LRFEngine::GetPageInfo(int pageNumber) const {
    if (pageNumber < 1 || pageNumber > static_cast<int>(pageInfos.size())) {
        return EBookPageInfo();
    }
    return pageInfos[pageNumber - 1];
}

std::vector<EBookTOCEntry> LRFEngine::GetTableOfContents() const {
    return tableOfContents;
}

std::string LRFEngine::GetPageContent(int pageNumber) const {
    if (pageNumber < 1 || pageNumber > static_cast<int>(parsedData.pages.size())) {
        return std::string();
    }
    return parsedData.pages[pageNumber - 1].content;
}

std::vector<uint8_t> LRFEngine::RenderPageToImage(int pageNumber,
                                                   int width, int height,
                                                   float dpi) const {
    // Rendering would use text layout engine
    return std::vector<uint8_t>();
}

std::vector<uint8_t> LRFEngine::GetCoverImage() const {
    return parsedData.thumbnailData;
}

const LRFParsedData& LRFEngine::GetParsedData() const {
    return parsedData;
}

uint16_t LRFEngine::GetVersion() const {
    return parsedData.version;
}

std::pair<int, int> LRFEngine::GetDocumentSize() const {
    return { parsedData.header.width, parsedData.header.height };
}

const LRFObject* LRFEngine::GetObject(uint32_t objectId) const {
    auto it = parsedData.objects.find(objectId);
    if (it != parsedData.objects.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<const LRFObject*> LRFEngine::GetImages() const {
    std::vector<const LRFObject*> images;
    
    for (const auto& [id, object] : parsedData.objects) {
        if (object.type == LRFObjectType::ImageStream && !object.imageData.empty()) {
            images.push_back(&object);
        }
    }
    
    return images;
}

// ============================================================================
// SEARCH
// ============================================================================

std::vector<EBookSearchResult> LRFEngine::Search(const std::string& query) const {
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

std::string LRFEngine::GetLastError() const {
    return lastError;
}

void LRFEngine::SetProgressCallback(std::function<void(float, const std::string&)> callback) {
    progressCallback = callback;
}

void LRFEngine::ReportProgress(float progress, const std::string& status) {
    if (progressCallback) {
        progressCallback(progress, status);
    }
}

// ============================================================================
// LRF DECOMPRESSOR IMPLEMENTATION
// ============================================================================

std::vector<uint8_t> LRFDecompressor::DecompressZlib(const uint8_t* data, size_t size,
                                                      size_t uncompressedSize) {
    // Check for ZLIB header
    if (size < 2 || !IsZlibCompressed(data, size)) {
        return std::vector<uint8_t>(data, data + size);
    }
    
    // Use EBookDecompression helper which leverages VirtualFS
    std::vector<uint8_t> input(data, data + size);
    std::vector<uint8_t> output;
    
    auto opts = EBookDecompressionOptions::ForZlib(uncompressedSize);
    
    if (EBookDecompression::DecompressDeflate(input, output, opts)) {
        return output;
    }
    
    // Fallback: return raw data
    return std::vector<uint8_t>(data, data + size);
}

bool LRFDecompressor::IsZlibCompressed(const uint8_t* data, size_t size) {
    if (size < 2) {
        return false;
    }
    
    // ZLIB header: CMF (compression method and flags) + FLG (flags)
    // CMF = 0x78 for deflate with 32K window
    // FLG values: 0x01, 0x5E, 0x9C, 0xDA depending on compression level
    
    uint8_t cmf = data[0];
    uint8_t flg = data[1];
    
    // Check for deflate compression (method 8)
    if ((cmf & 0x0F) != 8) {
        return false;
    }
    
    // Check CMF*256 + FLG is multiple of 31
    if (((cmf << 8) + flg) % 31 != 0) {
        return false;
    }
    
    return true;
}

// ============================================================================
// LRF TEXT PARSER IMPLEMENTATION
// ============================================================================

std::string LRFTextParser::ParseStream(const uint8_t* data, size_t size) {
    std::string result;
    
    // LRF text streams contain UTF-16LE text with embedded tags
    // Parse and extract text portions
    
    const uint8_t* ptr = data;
    const uint8_t* end = data + size;
    
    while (ptr + 2 <= end) {
        uint16_t value = LRFEngine::ReadLE16(ptr);
        
        // Check for tag markers (0xF5xx range)
        if ((value & 0xFF00) == 0xF500) {
            // Skip tag and its data
            ptr += 4; // Tag + data
            continue;
        }
        
        // Regular character (UTF-16LE)
        if (value >= 0x20 && value < 0xD800) {
            // ASCII or BMP character
            if (value < 0x80) {
                result.push_back(static_cast<char>(value));
            } else if (value < 0x800) {
                result.push_back(static_cast<char>(0xC0 | (value >> 6)));
                result.push_back(static_cast<char>(0x80 | (value & 0x3F)));
            } else {
                result.push_back(static_cast<char>(0xE0 | (value >> 12)));
                result.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
                result.push_back(static_cast<char>(0x80 | (value & 0x3F)));
            }
        } else if (value == 0x000A || value == 0x000D) {
            result.push_back('\n');
        }
        
        ptr += 2;
    }
    
    return result;
}

std::string LRFTextParser::ConvertText(const uint8_t* data, size_t size) {
    std::string result;
    
    for (size_t i = 0; i + 1 < size; i += 2) {
        uint16_t value = LRFEngine::ReadLE16(data + i);
        
        if (value < 0x80) {
            result.push_back(static_cast<char>(value));
        } else if (value < 0x800) {
            result.push_back(static_cast<char>(0xC0 | (value >> 6)));
            result.push_back(static_cast<char>(0x80 | (value & 0x3F)));
        } else {
            result.push_back(static_cast<char>(0xE0 | (value >> 12)));
            result.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (value & 0x3F)));
        }
    }
    
    return result;
}

std::string LRFTextParser::StripTags(const std::string& text) {
    // Remove any remaining tag-like content
    std::regex tagRegex(R"(\xF5[\x00-\xFF]{3})");
    return std::regex_replace(text, tagRegex, "");
}

// ============================================================================
// LRF DETECTION UTILITIES
// ============================================================================

namespace LRFDetection {

bool HasLRFExtension(const std::string& filePath) {
    size_t dotPos = filePath.find_last_of('.');
    if (dotPos == std::string::npos) {
        return false;
    }
    
    std::string ext = filePath.substr(dotPos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    return ext == "lrf" || ext == "lrs";
}

bool HasLRFMagic(const uint8_t* data, size_t size) {
    if (size < LRF_MAGIC_LENGTH) {
        return false;
    }
    
    return std::memcmp(data, LRF_MAGIC, LRF_MAGIC_LENGTH) == 0;
}

bool IsLRXFile(const std::string& filePath) {
    size_t dotPos = filePath.find_last_of('.');
    if (dotPos == std::string::npos) {
        return false;
    }
    
    std::string ext = filePath.substr(dotPos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    return ext == "lrx";
}

uint16_t GetVersion(const uint8_t* data, size_t size) {
    if (size < 6) {
        return 0;
    }
    
    // Check magic first
    if (!HasLRFMagic(data, size)) {
        return 0;
    }
    
    return LRFEngine::ReadLE16(data + 4);
}

} // namespace LRFDetection

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

std::unique_ptr<IEBookEngine> CreateLRFEngine() {
    return std::make_unique<LRFEngine>();
}

void RegisterLRFPlugin() {
    // Register with EBookEngineFactory
    // EBookEngineFactory::RegisterEngine("lrf", CreateLRFEngine);
    // EBookEngineFactory::RegisterEngine("lrs", CreateLRFEngine);
}

void UnregisterLRFPlugin() {
    // Unregister from EBookEngineFactory
    // EBookEngineFactory::UnregisterEngine("lrf");
    // EBookEngineFactory::UnregisterEngine("lrs");
}

} // namespace UltraCanvas
