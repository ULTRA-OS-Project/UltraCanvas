// Plugins/Documents/eBook/LITEngine.cpp
// Microsoft Reader LIT (Literature) eBook Engine Implementation
// Version: 1.0.0
// Last Modified: 2025-01-10
// Author: UltraCanvas Framework

#include "LITEngine.h"
#include "EBookDecompression.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <regex>

namespace UltraCanvas {

// ============================================================================
// BYTE ORDER HELPERS (LIT uses Little-Endian)
// ============================================================================

uint16_t LITEngine::ReadLE16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
           (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t LITEngine::ReadLE32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

uint64_t LITEngine::ReadLE64(const uint8_t* data) {
    return static_cast<uint64_t>(data[0]) |
           (static_cast<uint64_t>(data[1]) << 8) |
           (static_cast<uint64_t>(data[2]) << 16) |
           (static_cast<uint64_t>(data[3]) << 24) |
           (static_cast<uint64_t>(data[4]) << 32) |
           (static_cast<uint64_t>(data[5]) << 40) |
           (static_cast<uint64_t>(data[6]) << 48) |
           (static_cast<uint64_t>(data[7]) << 56);
}

uint64_t LITEngine::ReadEncodedInt(const uint8_t* data, size_t& bytesRead) {
    // Variable-length integer encoding used in LIT directory
    uint64_t result = 0;
    int shift = 0;
    bytesRead = 0;
    
    while (true) {
        uint8_t byte = data[bytesRead++];
        result |= static_cast<uint64_t>(byte & 0x7F) << shift;
        
        if ((byte & 0x80) == 0) {
            break;
        }
        
        shift += 7;
        
        if (bytesRead > 10) {
            // Prevent infinite loop
            break;
        }
    }
    
    return result;
}

// ============================================================================
// CONSTRUCTOR & DESTRUCTOR
// ============================================================================

LITEngine::LITEngine()
    : isLoaded(false)
    , pageCount(0) {
}

LITEngine::~LITEngine() {
    Close();
}

// ============================================================================
// FORMAT IDENTIFICATION
// ============================================================================

EBookFormat LITEngine::GetFormat() const {
    return EBookFormat::LIT;
}

std::string LITEngine::GetFormatName() const {
    return "Microsoft Reader (LIT)";
}

std::vector<std::string> LITEngine::GetSupportedExtensions() const {
    return { "lit" };
}

// ============================================================================
// DOCUMENT LOADING
// ============================================================================

bool LITEngine::LoadFile(const std::string& filePath, const std::string& password) {
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
    if (fileSize < LIT_MIN_FILE_SIZE) {
        lastError = "File too small to be valid LIT";
        return false;
    }
    
    file.seekg(0);
    fileData.resize(fileSize);
    file.read(reinterpret_cast<char*>(fileData.data()), fileSize);
    file.close();
    
    return LoadFromMemory(fileData, password);
}

bool LITEngine::LoadFromMemory(const std::vector<uint8_t>& data,
                                const std::string& password) {
    Close();
    
    if (data.size() < LIT_MIN_FILE_SIZE) {
        lastError = "Data too small to be valid LIT";
        return false;
    }
    
    // Store data for lazy extraction
    if (fileData.empty()) {
        fileData = data;
    }
    
    ReportProgress(0.1f, "Parsing LIT structure...");
    
    // Parse LIT file
    if (!ParseLIT(data.data(), data.size())) {
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

void LITEngine::Close() {
    parsedData = LITParsedData();
    metadata = EBookMetadata();
    tableOfContents.clear();
    pageInfos.clear();
    fileData.clear();
    isLoaded = false;
    pageCount = 0;
    lastError.clear();
}

bool LITEngine::IsLoaded() const {
    return isLoaded;
}

// ============================================================================
// VALIDATION
// ============================================================================

bool LITEngine::ValidateFile(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    // Read magic
    uint8_t magic[LIT_MAGIC_LENGTH];
    file.read(reinterpret_cast<char*>(magic), LIT_MAGIC_LENGTH);
    if (!file.good()) {
        return false;
    }
    
    return LITDetection::HasLITMagic(magic, LIT_MAGIC_LENGTH);
}

// ============================================================================
// STATIC DETECTION METHODS
// ============================================================================

bool LITEngine::IsLITFile(const std::string& filePath) {
    return LITDetection::HasLITExtension(filePath);
}

bool LITEngine::IsLITData(const uint8_t* data, size_t size) {
    return LITDetection::HasLITMagic(data, size);
}

// ============================================================================
// LIT PARSING
// ============================================================================

bool LITEngine::ParseLIT(const uint8_t* data, size_t size) {
    ReportProgress(0.2f, "Validating header...");
    
    // Validate header
    if (!ValidateHeader(data, size)) {
        return false;
    }
    
    // Check for DRM
    if (CheckDRM(data, size)) {
        parsedData.hasDRM = true;
        lastError = "DRM-protected LIT files are not supported";
        return false;
    }
    
    ReportProgress(0.3f, "Parsing headers...");
    
    // Parse initial header
    if (!ParseInitialHeader(data, size)) {
        return false;
    }
    
    // Parse header sections
    if (!ParseHeaderSections(data, size)) {
        return false;
    }
    
    // Parse ITSF header
    if (!ParseITSFHeader(data, size)) {
        return false;
    }
    
    ReportProgress(0.4f, "Parsing directory...");
    
    // Parse directory
    if (!ParseDirectory(data, size)) {
        return false;
    }
    
    ReportProgress(0.5f, "Extracting content files...");
    
    // Parse content files
    if (!ParseContentFiles(data, size)) {
        return false;
    }
    
    ReportProgress(0.7f, "Parsing metadata...");
    
    // Find and parse OPF
    for (const auto& [path, entry] : parsedData.directory) {
        if (path.find(".opf") != std::string::npos) {
            parsedData.opfPath = path;
            auto it = parsedData.contentFiles.find(path);
            if (it != parsedData.contentFiles.end()) {
                std::string opfContent(it->second.data.begin(), it->second.data.end());
                ParseOPF(opfContent);
            }
            break;
        }
    }
    
    // Build EBookMetadata
    metadata.title = parsedData.metadata.title;
    metadata.author = parsedData.metadata.author;
    metadata.publisher = parsedData.metadata.publisher;
    metadata.description = parsedData.metadata.description;
    metadata.language = parsedData.metadata.language;
    
    // Use filename as title if not found
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

bool LITEngine::ValidateHeader(const uint8_t* data, size_t size) {
    // Check ITOLITLS signature
    if (size < LIT_MAGIC_LENGTH) {
        lastError = "File too small for LIT header";
        return false;
    }
    
    if (std::memcmp(data, LIT_MAGIC_ITOL, LIT_MAGIC_LENGTH) != 0) {
        lastError = "Invalid LIT magic signature (ITOLITLS)";
        return false;
    }
    
    // Check ITSF signature at offset 0xC8
    if (size >= LIT_ITSF_OFFSET + LIT_ITSF_LENGTH) {
        if (std::memcmp(data + LIT_ITSF_OFFSET, LIT_MAGIC_ITSF, LIT_ITSF_LENGTH) != 0) {
            // ITSF not at expected offset, try to find it
            // Some LIT files have different layout
        }
    }
    
    parsedData.hasValidHeader = true;
    return true;
}

bool LITEngine::ParseInitialHeader(const uint8_t* data, size_t size) {
    if (size < sizeof(LITInitialHeader)) {
        lastError = "File too small for initial header";
        return false;
    }
    
    std::memcpy(parsedData.initialHeader.signature, data, 8);
    parsedData.initialHeader.version = ReadLE32(data + 8);
    parsedData.initialHeader.headerLength = ReadLE32(data + 12);
    parsedData.initialHeader.numHeaderSections = ReadLE32(data + 16);
    parsedData.initialHeader.unknown1 = ReadLE32(data + 20);
    parsedData.initialHeader.unknown2 = ReadLE32(data + 24);
    parsedData.initialHeader.totalFileLength = ReadLE64(data + 28);
    
    parsedData.version = parsedData.initialHeader.version;
    
    return true;
}

bool LITEngine::ParseHeaderSections(const uint8_t* data, size_t size) {
    // Header sections start after initial header
    size_t offset = 36;
    uint32_t numSections = parsedData.initialHeader.numHeaderSections;
    
    if (numSections > 10) {
        // Sanity check
        numSections = 5;  // Standard LIT has 5 sections
    }
    
    parsedData.headerSections.clear();
    parsedData.headerSections.reserve(numSections);
    
    for (uint32_t i = 0; i < numSections && offset + 16 <= size; ++i) {
        LITHeaderSectionEntry entry;
        entry.offset = ReadLE64(data + offset);
        entry.length = ReadLE64(data + offset + 8);
        
        if (entry.offset < size) {
            parsedData.headerSections.push_back(entry);
        }
        
        offset += 16;
    }
    
    return !parsedData.headerSections.empty();
}

bool LITEngine::ParseITSFHeader(const uint8_t* data, size_t size) {
    if (size < LIT_ITSF_OFFSET + sizeof(LITITSFHeader)) {
        return true;  // Optional header
    }
    
    const uint8_t* ptr = data + LIT_ITSF_OFFSET;
    
    if (std::memcmp(ptr, LIT_MAGIC_ITSF, LIT_ITSF_LENGTH) == 0) {
        std::memcpy(parsedData.itsfHeader.signature, ptr, 4);
        parsedData.itsfHeader.version = ReadLE32(ptr + 4);
        parsedData.itsfHeader.headerLength = ReadLE32(ptr + 8);
        parsedData.itsfHeader.unknown1 = ReadLE32(ptr + 12);
        parsedData.itsfHeader.contentOffset = ReadLE64(ptr + 16);
        parsedData.itsfHeader.timestamp = ReadLE32(ptr + 24);
        parsedData.itsfHeader.languageId = ReadLE32(ptr + 28);
    }
    
    return true;
}

bool LITEngine::ParseDirectory(const uint8_t* data, size_t size) {
    // Find directory (IFCM signature)
    // Usually in header section 1 or at a specific offset
    
    // Look for IFCM signature
    size_t dirOffset = 0;
    for (size_t i = 0; i + 4 < size; ++i) {
        if (std::memcmp(data + i, LIT_DIR_MAGIC, LIT_DIR_MAGIC_LENGTH) == 0) {
            dirOffset = i;
            break;
        }
    }
    
    if (dirOffset == 0) {
        // Try using header section offset
        if (parsedData.headerSections.size() >= 2) {
            dirOffset = static_cast<size_t>(parsedData.headerSections[1].offset);
        }
    }
    
    if (dirOffset == 0 || dirOffset + sizeof(LITDirectoryHeader) > size) {
        lastError = "Could not find directory";
        return false;
    }
    
    // Parse directory header
    const uint8_t* ptr = data + dirOffset;
    std::memcpy(parsedData.directoryHeader.signature, ptr, 4);
    parsedData.directoryHeader.version = ReadLE32(ptr + 4);
    parsedData.directoryHeader.chunkSize = ReadLE32(ptr + 8);
    parsedData.directoryHeader.unknown1 = ReadLE32(ptr + 12);
    parsedData.directoryHeader.unknown2 = static_cast<int32_t>(ReadLE32(ptr + 16));
    parsedData.directoryHeader.unknown3 = static_cast<int32_t>(ReadLE32(ptr + 20));
    parsedData.directoryHeader.numChunks = ReadLE32(ptr + 24);
    
    // Parse directory chunks
    size_t chunkOffset = dirOffset + 32;
    uint32_t chunkSize = parsedData.directoryHeader.chunkSize;
    
    if (chunkSize == 0) {
        chunkSize = 0x2000;  // Default
    }
    
    for (uint32_t i = 0; i < parsedData.directoryHeader.numChunks && 
         chunkOffset + chunkSize <= size; ++i) {
        ParseDirectoryChunk(data, size, chunkOffset);
        chunkOffset += chunkSize;
    }
    
    return !parsedData.directory.empty();
}

bool LITEngine::ParseDirectoryChunk(const uint8_t* data, size_t size, size_t offset) {
    if (offset + 4 > size) {
        return false;
    }
    
    const uint8_t* ptr = data + offset;
    uint8_t chunkType = ptr[0];
    
    if (chunkType != static_cast<uint8_t>(LITChunkType::Listing)) {
        return true;  // Skip non-listing chunks
    }
    
    // Listing chunk format:
    // 'L', 'M' or 'P', free space, num entries, then entries
    
    uint32_t numEntries = ReadLE32(ptr + 4);
    size_t entryOffset = 8;  // Start of entries
    
    for (uint32_t i = 0; i < numEntries && entryOffset + 4 < parsedData.directoryHeader.chunkSize; ++i) {
        // Read entry
        size_t bytesRead;
        
        // Name length (encoded int)
        uint64_t nameLen = ReadEncodedInt(ptr + entryOffset, bytesRead);
        entryOffset += bytesRead;
        
        if (nameLen == 0 || entryOffset + nameLen > parsedData.directoryHeader.chunkSize) {
            break;
        }
        
        // Name
        std::string name(reinterpret_cast<const char*>(ptr + entryOffset), nameLen);
        entryOffset += nameLen;
        
        // Section (encoded int)
        uint64_t section = ReadEncodedInt(ptr + entryOffset, bytesRead);
        entryOffset += bytesRead;
        
        // Offset (encoded int)
        uint64_t fileOffset = ReadEncodedInt(ptr + entryOffset, bytesRead);
        entryOffset += bytesRead;
        
        // Length (encoded int)
        uint64_t length = ReadEncodedInt(ptr + entryOffset, bytesRead);
        entryOffset += bytesRead;
        
        // Create entry
        LITDirectoryEntry entry;
        entry.name = name;
        entry.section = static_cast<uint32_t>(section);
        entry.offset = fileOffset;
        entry.length = length;
        
        parsedData.directory[name] = entry;
    }
    
    return true;
}

bool LITEngine::ParseContentFiles(const uint8_t* data, size_t size) {
    for (const auto& [path, entry] : parsedData.directory) {
        // Skip internal files
        if (path.empty() || path[0] == ':' || path[0] == '/') {
            continue;
        }
        
        LITContentFile file;
        if (ExtractFile(data, size, entry, file)) {
            file.path = path;
            file.mimeType = GetMimeType(path);
            parsedData.contentFiles[path] = std::move(file);
        }
    }
    
    // Extract full text content from HTML files
    std::ostringstream fullText;
    for (const auto& [path, file] : parsedData.contentFiles) {
        if (file.mimeType == "text/html" || file.mimeType == "application/xhtml+xml") {
            std::string content(file.data.begin(), file.data.end());
            std::string text = ExtractTextFromHTML(content);
            fullText << text << "\n";
        }
    }
    parsedData.fullTextContent = fullText.str();
    
    return !parsedData.contentFiles.empty();
}

bool LITEngine::ExtractFile(const uint8_t* data, size_t size,
                             const LITDirectoryEntry& entry,
                             LITContentFile& file) {
    // Find content section offset
    uint64_t contentOffset = 0;
    
    if (parsedData.itsfHeader.contentOffset > 0) {
        contentOffset = parsedData.itsfHeader.contentOffset;
    } else if (!parsedData.headerSections.empty()) {
        // Use first header section as content base
        contentOffset = parsedData.headerSections[0].offset;
    }
    
    uint64_t fileOffset = contentOffset + entry.offset;
    
    if (fileOffset + entry.length > size) {
        return false;
    }
    
    // Check if compressed (LZX)
    // Content in section 1 is typically compressed
    if (entry.section == 1) {
        file.isCompressed = true;
        parsedData.isCompressed = true;
        
        // Attempt decompression
        file.data = DecompressLZX(data + fileOffset, 
                                   static_cast<size_t>(entry.length),
                                   static_cast<size_t>(entry.length * 10));
        
        if (file.data.empty()) {
            // Decompression failed, store raw data
            file.data.assign(data + fileOffset, data + fileOffset + entry.length);
        }
    } else {
        file.isCompressed = false;
        file.data.assign(data + fileOffset, data + fileOffset + entry.length);
    }
    
    return !file.data.empty();
}

std::vector<uint8_t> LITEngine::DecompressLZX(const uint8_t* data, size_t size,
                                               size_t uncompressedSize) {
    // Use EBookDecompression helper which leverages VirtualFS LZX support
    std::vector<uint8_t> input(data, data + size);
    std::vector<uint8_t> output;
    
    // Check if data looks like LZX compressed
    if (size >= 4 && std::memcmp(data, LIT_LZX_MAGIC, 4) == 0) {
        // LZX compressed - use VirtualFS
        if (EBookDecompression::DecompressLZX(input, output, 0x10000, 0x10000)) {
            return output;
        }
    }
    
    // Check if data looks like plain text/HTML (uncompressed)
    bool looksUncompressed = false;
    for (size_t i = 0; i < std::min(size, size_t(100)); ++i) {
        if (data[i] == '<' || data[i] == '?' || 
            (data[i] >= 0x20 && data[i] < 0x7F)) {
            looksUncompressed = true;
            break;
        }
    }
    
    if (looksUncompressed) {
        return std::vector<uint8_t>(data, data + size);
    }
    
    // Return raw data as fallback
    return std::vector<uint8_t>(data, data + size);
}

bool LITEngine::CheckDRM(const uint8_t* data, size_t size) {
    // Check for DRM markers
    // DRM-protected files have specific patterns
    
    // Look for DRM-related strings
    const char* drmMarkers[] = {
        "DRMSource",
        "DRM",
        "ms-its:drmLicense"
    };
    
    std::string content(reinterpret_cast<const char*>(data), 
                        std::min(size, size_t(10000)));
    
    for (const auto& marker : drmMarkers) {
        if (content.find(marker) != std::string::npos) {
            return true;
        }
    }
    
    // Check for encrypted content markers
    // (These would be specific byte patterns)
    
    return false;
}

bool LITEngine::ParseOPF(const std::string& opfContent) {
    // Parse Dublin Core metadata
    ParseMetadata(opfContent);
    
    // Parse spine
    std::regex itemrefRegex(R"(<itemref\s+idref=[\"']([^\"']+)[\"'])");
    std::regex itemRegex(R"(<item\s+[^>]*id=[\"']([^\"']+)[\"'][^>]*href=[\"']([^\"']+)[\"'][^>]*media-type=[\"']([^\"']+)[\"'])");
    
    // Find all items
    std::map<std::string, std::pair<std::string, std::string>> items; // id -> (href, mediaType)
    std::smatch match;
    std::string::const_iterator searchStart(opfContent.cbegin());
    
    while (std::regex_search(searchStart, opfContent.cend(), match, itemRegex)) {
        items[match[1].str()] = {match[2].str(), match[3].str()};
        searchStart = match.suffix().first;
    }
    
    // Find spine itemrefs
    searchStart = opfContent.cbegin();
    while (std::regex_search(searchStart, opfContent.cend(), match, itemrefRegex)) {
        std::string idref = match[1].str();
        auto it = items.find(idref);
        if (it != items.end()) {
            LITSpineItem spineItem;
            spineItem.id = idref;
            spineItem.href = it->second.first;
            spineItem.mediaType = it->second.second;
            parsedData.spine.push_back(spineItem);
        }
        searchStart = match.suffix().first;
    }
    
    return true;
}

bool LITEngine::ParseMetadata(const std::string& xmlContent) {
    // Extract Dublin Core metadata
    std::regex titleRegex(R"(<dc:title[^>]*>([^<]+)</dc:title>)", std::regex::icase);
    std::regex authorRegex(R"(<dc:creator[^>]*>([^<]+)</dc:creator>)", std::regex::icase);
    std::regex publisherRegex(R"(<dc:publisher[^>]*>([^<]+)</dc:publisher>)", std::regex::icase);
    std::regex descRegex(R"(<dc:description[^>]*>([^<]+)</dc:description>)", std::regex::icase);
    std::regex languageRegex(R"(<dc:language[^>]*>([^<]+)</dc:language>)", std::regex::icase);
    std::regex isbnRegex(R"(<dc:identifier[^>]*>([^<]+)</dc:identifier>)", std::regex::icase);
    
    std::smatch match;
    
    if (std::regex_search(xmlContent, match, titleRegex)) {
        parsedData.metadata.title = match[1].str();
    }
    if (std::regex_search(xmlContent, match, authorRegex)) {
        parsedData.metadata.author = match[1].str();
    }
    if (std::regex_search(xmlContent, match, publisherRegex)) {
        parsedData.metadata.publisher = match[1].str();
    }
    if (std::regex_search(xmlContent, match, descRegex)) {
        parsedData.metadata.description = match[1].str();
    }
    if (std::regex_search(xmlContent, match, languageRegex)) {
        parsedData.metadata.language = match[1].str();
    }
    if (std::regex_search(xmlContent, match, isbnRegex)) {
        parsedData.metadata.isbn = match[1].str();
    }
    
    // Look for cover image
    std::regex coverRegex(R"(<meta\s+[^>]*name=[\"']cover[\"'][^>]*content=[\"']([^\"']+)[\"'])");
    if (std::regex_search(xmlContent, match, coverRegex)) {
        std::string coverId = match[1].str();
        
        // Find item with this ID
        std::regex coverItemRegex(R"(<item\s+[^>]*id=[\"'])" + coverId + R"([\"'][^>]*href=[\"']([^\"']+)[\"'])");
        if (std::regex_search(xmlContent, match, coverItemRegex)) {
            std::string coverPath = match[1].str();
            auto it = parsedData.contentFiles.find(coverPath);
            if (it != parsedData.contentFiles.end()) {
                parsedData.coverImage = it->second.data;
                parsedData.coverMimeType = it->second.mimeType;
            }
        }
    }
    
    return true;
}

std::string LITEngine::ExtractTextFromHTML(const std::string& html) {
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
    std::regex quotRegex(R"(&quot;)");
    
    cleaned = std::regex_replace(cleaned, nbspRegex, " ");
    cleaned = std::regex_replace(cleaned, ltRegex, "<");
    cleaned = std::regex_replace(cleaned, gtRegex, ">");
    cleaned = std::regex_replace(cleaned, ampRegex, "&");
    cleaned = std::regex_replace(cleaned, quotRegex, "\"");
    
    // Collapse whitespace
    std::regex whitespaceRegex(R"(\s+)");
    result = std::regex_replace(cleaned, whitespaceRegex, " ");
    
    // Trim
    size_t start = result.find_first_not_of(" \t\n\r");
    size_t end = result.find_last_not_of(" \t\n\r");
    
    if (start != std::string::npos && end != std::string::npos) {
        result = result.substr(start, end - start + 1);
    }
    
    return result;
}

std::string LITEngine::GetMimeType(const std::string& path) const {
    size_t dotPos = path.find_last_of('.');
    if (dotPos == std::string::npos) {
        return "application/octet-stream";
    }
    
    std::string ext = path.substr(dotPos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    if (ext == "html" || ext == "htm") return "text/html";
    if (ext == "xhtml") return "application/xhtml+xml";
    if (ext == "xml") return "application/xml";
    if (ext == "opf") return "application/oebps-package+xml";
    if (ext == "ncx") return "application/x-dtbncx+xml";
    if (ext == "css") return "text/css";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "png") return "image/png";
    if (ext == "gif") return "image/gif";
    if (ext == "svg") return "image/svg+xml";
    
    return "application/octet-stream";
}

// ============================================================================
// PAGE STRUCTURE
// ============================================================================

void LITEngine::BuildPageStructure() {
    // Estimate pages from content
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

void LITEngine::BuildTableOfContents() {
    tableOfContents.clear();
    
    // Try to build from spine
    int pageNum = 1;
    for (const auto& item : parsedData.spine) {
        EBookTOCEntry entry;
        entry.title = item.href;
        entry.level = 0;
        entry.pageNumber = pageNum++;
        tableOfContents.push_back(entry);
    }
    
    // If no spine, create basic entry
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

EBookMetadata LITEngine::GetMetadata() const {
    return metadata;
}

int LITEngine::GetPageCount() const {
    return pageCount;
}

EBookPageInfo LITEngine::GetPageInfo(int pageNumber) const {
    if (pageNumber < 1 || pageNumber > static_cast<int>(pageInfos.size())) {
        return EBookPageInfo();
    }
    return pageInfos[pageNumber - 1];
}

std::vector<EBookTOCEntry> LITEngine::GetTableOfContents() const {
    return tableOfContents;
}

std::string LITEngine::GetPageContent(int pageNumber) const {
    if (pageNumber < 1 || pageNumber > pageCount) {
        return std::string();
    }
    
    const auto& info = pageInfos[pageNumber - 1];
    if (info.contentOffset >= static_cast<int>(parsedData.fullTextContent.length())) {
        return std::string();
    }
    
    return parsedData.fullTextContent.substr(info.contentOffset, info.contentLength);
}

std::vector<uint8_t> LITEngine::RenderPageToImage(int pageNumber,
                                                   int width, int height,
                                                   float dpi) const {
    // Rendering would use text layout engine
    return std::vector<uint8_t>();
}

std::vector<uint8_t> LITEngine::GetCoverImage() const {
    return parsedData.coverImage;
}

const LITParsedData& LITEngine::GetParsedData() const {
    return parsedData;
}

bool LITEngine::HasDRM() const {
    return parsedData.hasDRM;
}

const LITContentFile* LITEngine::GetContentFile(const std::string& path) const {
    auto it = parsedData.contentFiles.find(path);
    if (it != parsedData.contentFiles.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<std::string> LITEngine::GetContentFilePaths() const {
    std::vector<std::string> paths;
    paths.reserve(parsedData.contentFiles.size());
    
    for (const auto& [path, file] : parsedData.contentFiles) {
        paths.push_back(path);
    }
    
    return paths;
}

std::string LITEngine::GetHTMLContent(const std::string& path) const {
    auto* file = GetContentFile(path);
    if (file && (file->mimeType == "text/html" || 
                 file->mimeType == "application/xhtml+xml")) {
        return std::string(file->data.begin(), file->data.end());
    }
    return std::string();
}

// ============================================================================
// SEARCH
// ============================================================================

std::vector<EBookSearchResult> LITEngine::Search(const std::string& query) const {
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

std::string LITEngine::GetLastError() const {
    return lastError;
}

void LITEngine::SetProgressCallback(std::function<void(float, const std::string&)> callback) {
    progressCallback = callback;
}

void LITEngine::ReportProgress(float progress, const std::string& status) {
    if (progressCallback) {
        progressCallback(progress, status);
    }
}

// ============================================================================
// LZX DECOMPRESSOR IMPLEMENTATION
// ============================================================================

LITLZXDecompressor::LITLZXDecompressor()
    : windowSize(0)
    , resetInterval(0)
    , initialized(false)
    , windowPosition(0) {
}

LITLZXDecompressor::~LITLZXDecompressor() {
}

bool LITLZXDecompressor::Initialize(uint32_t windowSize, uint32_t resetInterval) {
    this->windowSize = windowSize;
    this->resetInterval = resetInterval;
    window.resize(windowSize);
    windowPosition = 0;
    initialized = true;
    return true;
}

std::vector<uint8_t> LITLZXDecompressor::Decompress(const uint8_t* data, size_t size,
                                                     size_t uncompressedSize) {
    // Full LZX decompression would be implemented here
    // LZX is a complex algorithm similar to LZMA
    // For production, use a library like libmspack
    
    return std::vector<uint8_t>(data, data + size);
}

void LITLZXDecompressor::Reset() {
    windowPosition = 0;
    std::fill(window.begin(), window.end(), 0);
}

// ============================================================================
// LIT DETECTION UTILITIES
// ============================================================================

namespace LITDetection {

bool HasLITExtension(const std::string& filePath) {
    size_t dotPos = filePath.find_last_of('.');
    if (dotPos == std::string::npos) {
        return false;
    }
    
    std::string ext = filePath.substr(dotPos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    return ext == "lit";
}

bool HasLITMagic(const uint8_t* data, size_t size) {
    if (size < LIT_MAGIC_LENGTH) {
        return false;
    }
    
    return std::memcmp(data, LIT_MAGIC_ITOL, LIT_MAGIC_LENGTH) == 0;
}

bool HasITSFSignature(const uint8_t* data, size_t size) {
    if (size < LIT_ITSF_OFFSET + LIT_ITSF_LENGTH) {
        return false;
    }
    
    return std::memcmp(data + LIT_ITSF_OFFSET, LIT_MAGIC_ITSF, LIT_ITSF_LENGTH) == 0;
}

bool HasDRMMarker(const uint8_t* data, size_t size) {
    // Check for DRM-related content
    std::string content(reinterpret_cast<const char*>(data), 
                        std::min(size, size_t(10000)));
    
    return content.find("DRMSource") != std::string::npos ||
           content.find("ms-its:drmLicense") != std::string::npos;
}

uint32_t GetVersion(const uint8_t* data, size_t size) {
    if (size < 12) {
        return 0;
    }
    
    if (!HasLITMagic(data, size)) {
        return 0;
    }
    
    return LITEngine::ReadLE32(data + 8);
}

} // namespace LITDetection

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

std::unique_ptr<IEBookEngine> CreateLITEngine() {
    return std::make_unique<LITEngine>();
}

void RegisterLITPlugin() {
    // Register with EBookEngineFactory
    // EBookEngineFactory::RegisterEngine("lit", CreateLITEngine);
}

void UnregisterLITPlugin() {
    // Unregister from EBookEngineFactory
    // EBookEngineFactory::UnregisterEngine("lit");
}

} // namespace UltraCanvas
