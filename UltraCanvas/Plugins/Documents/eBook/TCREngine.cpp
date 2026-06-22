// Plugins/Documents/eBook/TCREngine.cpp
// Psion Series 3 TCR (Text Compression for Reader) eBook Engine Implementation
// Version: 1.0.0
// Last Modified: 2025-01-10
// Author: UltraCanvas Framework

#include "TCREngine.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <map>

namespace UltraCanvas {

// ============================================================================
// CONSTRUCTOR & DESTRUCTOR
// ============================================================================

TCREngine::TCREngine()
    : isLoaded(false)
    , pageCount(0) {
}

TCREngine::~TCREngine() {
    Close();
}

// ============================================================================
// FORMAT IDENTIFICATION
// ============================================================================

EBookFormat TCREngine::GetFormat() const {
    return EBookFormat::TCR;
}

std::string TCREngine::GetFormatName() const {
    return "Psion TCR (Text Compression for Reader)";
}

std::vector<std::string> TCREngine::GetSupportedExtensions() const {
    return { "tcr" };
}

// ============================================================================
// DOCUMENT LOADING
// ============================================================================

bool TCREngine::LoadFile(const std::string& filePath, const std::string& password) {
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
    if (fileSize < TCR_MAGIC_LENGTH) {
        lastError = "File too small to be valid TCR";
        return false;
    }
    
    file.seekg(0);
    std::vector<uint8_t> data(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);
    file.close();
    
    return LoadFromMemory(data, password);
}

bool TCREngine::LoadFromMemory(const std::vector<uint8_t>& data,
                                const std::string& password) {
    Close();
    
    if (data.size() < TCR_MAGIC_LENGTH) {
        lastError = "Data too small to be valid TCR";
        return false;
    }
    
    ReportProgress(0.1f, "Parsing TCR structure...");
    
    // Parse TCR file
    if (!ParseTCR(data.data(), data.size())) {
        return false;
    }
    
    ReportProgress(0.7f, "Building metadata...");
    
    // Build metadata
    BuildMetadata();
    
    ReportProgress(0.8f, "Building table of contents...");
    
    // Build table of contents
    BuildTableOfContents();
    
    // Calculate pages
    CalculatePages();
    
    isLoaded = true;
    
    ReportProgress(1.0f, "Complete");
    
    return true;
}

void TCREngine::Close() {
    parsedData = TCRParsedData();
    metadata = EBookMetadata();
    tableOfContents.clear();
    pageInfos.clear();
    isLoaded = false;
    pageCount = 0;
    lastError.clear();
}

bool TCREngine::IsLoaded() const {
    return isLoaded;
}

// ============================================================================
// VALIDATION
// ============================================================================

bool TCREngine::ValidateFile(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    // Read magic
    uint8_t magic[TCR_MAGIC_LENGTH];
    file.read(reinterpret_cast<char*>(magic), TCR_MAGIC_LENGTH);
    if (!file.good()) {
        return false;
    }
    
    return HasTCRMagic(magic, TCR_MAGIC_LENGTH);
}

// ============================================================================
// STATIC DETECTION METHODS
// ============================================================================

bool TCREngine::IsTCRFile(const std::string& filePath) {
    return TCRDetection::HasTCRExtension(filePath);
}

bool TCREngine::IsTCRData(const uint8_t* data, size_t size) {
    return TCRDetection::HasTCRMagic(data, size);
}

bool TCREngine::HasTCRMagic(const uint8_t* data, size_t size) {
    return TCRDetection::HasTCRMagic(data, size);
}

// ============================================================================
// TCR PARSING
// ============================================================================

bool TCREngine::ParseTCR(const uint8_t* data, size_t size) {
    ReportProgress(0.2f, "Validating magic signature...");
    
    // Validate magic
    if (!ValidateMagic(data, size)) {
        lastError = "Invalid TCR magic signature";
        return false;
    }
    
    parsedData.hasValidMagic = true;
    parsedData.compressedSize = size;
    
    ReportProgress(0.3f, "Parsing dictionary...");
    
    // Parse dictionary
    size_t offset = TCR_MAGIC_LENGTH;
    if (!ParseDictionary(data, size, offset)) {
        lastError = "Failed to parse TCR dictionary";
        return false;
    }
    
    parsedData.compressedDataOffset = offset;
    
    ReportProgress(0.5f, "Decompressing content...");
    
    // Decompress content
    if (!DecompressContent(data, size, offset)) {
        lastError = "Failed to decompress TCR content";
        return false;
    }
    
    // Calculate statistics
    parsedData.uncompressedSize = parsedData.textContent.length();
    if (parsedData.compressedSize > 0) {
        parsedData.compressionRatio = 
            100.0f * (1.0f - static_cast<float>(parsedData.compressedSize) / 
                             static_cast<float>(parsedData.uncompressedSize));
    }
    
    return true;
}

bool TCREngine::ValidateMagic(const uint8_t* data, size_t size) {
    if (size < TCR_MAGIC_LENGTH) {
        return false;
    }
    
    return std::memcmp(data, TCR_MAGIC, TCR_MAGIC_LENGTH) == 0;
}

bool TCREngine::ParseDictionary(const uint8_t* data, size_t size, size_t& offset) {
    // TCR dictionary format:
    // For each of 256 symbols (0x00-0xFF):
    //   - 1 byte: length of replacement string (N)
    //   - N bytes: replacement string
    //
    // Note: The exact format can vary between TCR implementations.
    // Some use fixed-length entries, others use length-prefixed strings.
    
    // Initialize dictionary with single-byte entries
    for (int i = 0; i < 256; ++i) {
        parsedData.dictionary[i].replacement = std::string(1, static_cast<char>(i));
    }
    
    // Read dictionary entries
    // TCR format: After magic, each symbol 0-255 has its replacement string
    // The replacement string is length-prefixed (1 byte length + string)
    
    for (int i = 0; i < 256 && offset < size; ++i) {
        // Read length byte
        uint8_t len = data[offset++];
        
        if (len == 0) {
            // Zero length means this symbol maps to itself
            parsedData.dictionary[i].replacement = std::string(1, static_cast<char>(i));
        } else {
            // Read replacement string
            if (offset + len > size) {
                // Truncated dictionary, use remaining data
                len = static_cast<uint8_t>(size - offset);
            }
            
            parsedData.dictionary[i].replacement = 
                std::string(reinterpret_cast<const char*>(data + offset), len);
            offset += len;
        }
    }
    
    return true;
}

bool TCREngine::DecompressContent(const uint8_t* data, size_t size, size_t offset) {
    // Decompress using dictionary
    TCRDecompressor decompressor;
    if (!decompressor.Initialize(parsedData.dictionary)) {
        return false;
    }
    
    parsedData.textContent = decompressor.Decompress(data + offset, size - offset);
    
    return !parsedData.textContent.empty() || (size - offset) == 0;
}

// ============================================================================
// METADATA BUILDING
// ============================================================================

void TCREngine::BuildMetadata() {
    // TCR format doesn't support metadata, so we extract what we can
    
    // Title from filename
    metadata.title = ExtractTitleFromPath(currentFilePath);
    
    // Try to extract title from first line of content
    if (!parsedData.textContent.empty()) {
        size_t firstNewline = parsedData.textContent.find('\n');
        if (firstNewline != std::string::npos && firstNewline < 100) {
            std::string firstLine = parsedData.textContent.substr(0, firstNewline);
            // Remove leading/trailing whitespace
            firstLine.erase(0, firstLine.find_first_not_of(" \t\r\n"));
            firstLine.erase(firstLine.find_last_not_of(" \t\r\n") + 1);
            
            // Use first line as title if it looks like a title
            if (!firstLine.empty() && firstLine.length() < 80) {
                metadata.title = firstLine;
            }
        }
    }
    
    // No author info in TCR format
    metadata.author = "Unknown";
    
    // Format description
    metadata.description = "Psion TCR eBook (compressed text)";
    
    // Language - assume English
    metadata.language = "en";
}

std::string TCREngine::ExtractTitleFromPath(const std::string& filePath) const {
    // Extract filename without extension
    size_t lastSlash = filePath.find_last_of("/\\");
    size_t lastDot = filePath.find_last_of('.');
    
    std::string filename;
    if (lastSlash != std::string::npos) {
        filename = filePath.substr(lastSlash + 1);
    } else {
        filename = filePath;
    }
    
    if (lastDot != std::string::npos && lastDot > (lastSlash != std::string::npos ? lastSlash : 0)) {
        filename = filename.substr(0, filename.find_last_of('.'));
    }
    
    // Replace underscores with spaces
    std::replace(filename.begin(), filename.end(), '_', ' ');
    
    return filename;
}

// ============================================================================
// TABLE OF CONTENTS
// ============================================================================

void TCREngine::BuildTableOfContents() {
    tableOfContents.clear();
    
    // TCR doesn't have formal chapter structure
    // Try to find chapters based on common patterns
    
    const std::string& content = parsedData.textContent;
    
    // Look for patterns like "Chapter 1", "CHAPTER ONE", "Part I", etc.
    std::vector<std::pair<std::string, size_t>> patterns = {
        {"Chapter ", 0},
        {"CHAPTER ", 0},
        {"Part ", 0},
        {"PART ", 0},
        {"Book ", 0},
        {"BOOK ", 0},
        {"Section ", 0},
        {"SECTION ", 0}
    };
    
    for (const auto& pattern : patterns) {
        size_t pos = 0;
        int chapterNum = 1;
        
        while ((pos = content.find(pattern.first, pos)) != std::string::npos) {
            // Check if it's at start of line
            if (pos > 0 && content[pos - 1] != '\n' && content[pos - 1] != '\r') {
                pos += pattern.first.length();
                continue;
            }
            
            // Extract chapter title (to end of line)
            size_t endLine = content.find('\n', pos);
            if (endLine == std::string::npos) {
                endLine = content.length();
            }
            
            std::string title = content.substr(pos, endLine - pos);
            // Trim whitespace
            title.erase(title.find_last_not_of(" \t\r\n") + 1);
            
            EBookTOCEntry entry;
            entry.title = title;
            entry.level = 0;
            
            // Calculate page number
            entry.pageNumber = 1;
            if (!pageInfos.empty()) {
                for (size_t i = 0; i < pageInfos.size(); ++i) {
                    if (static_cast<size_t>(pageInfos[i].contentOffset) <= pos) {
                        entry.pageNumber = static_cast<int>(i) + 1;
                    } else {
                        break;
                    }
                }
            }
            
            tableOfContents.push_back(entry);
            pos = endLine;
            ++chapterNum;
        }
        
        // If we found chapters, stop looking
        if (!tableOfContents.empty()) {
            break;
        }
    }
    
    // If no chapters found, create basic structure
    if (tableOfContents.empty() && pageCount > 0) {
        EBookTOCEntry entry;
        entry.title = metadata.title;
        entry.level = 0;
        entry.pageNumber = 1;
        tableOfContents.push_back(entry);
    }
}

// ============================================================================
// PAGE CALCULATION
// ============================================================================

void TCREngine::CalculatePages() {
    // Estimate pages based on content length
    // Typical page: ~2000 characters
    const int CHARS_PER_PAGE = 2000;
    
    size_t contentLength = parsedData.textContent.length();
    pageCount = std::max(1, static_cast<int>((contentLength + CHARS_PER_PAGE - 1) / CHARS_PER_PAGE));
    
    // Build page info
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

// ============================================================================
// CONTENT ACCESS
// ============================================================================

EBookMetadata TCREngine::GetMetadata() const {
    return metadata;
}

int TCREngine::GetPageCount() const {
    return pageCount;
}

EBookPageInfo TCREngine::GetPageInfo(int pageNumber) const {
    if (pageNumber < 1 || pageNumber > static_cast<int>(pageInfos.size())) {
        return EBookPageInfo();
    }
    return pageInfos[pageNumber - 1];
}

std::vector<EBookTOCEntry> TCREngine::GetTableOfContents() const {
    return tableOfContents;
}

std::string TCREngine::GetPageContent(int pageNumber) const {
    if (pageNumber < 1 || pageNumber > pageCount) {
        return std::string();
    }
    
    const auto& info = pageInfos[pageNumber - 1];
    if (info.contentOffset >= static_cast<int>(parsedData.textContent.length())) {
        return std::string();
    }
    
    return parsedData.textContent.substr(info.contentOffset, info.contentLength);
}

std::vector<uint8_t> TCREngine::RenderPageToImage(int pageNumber,
                                                   int width, int height,
                                                   float dpi) const {
    // Text-only format, rendering would need text layout engine
    return std::vector<uint8_t>();
}

std::string TCREngine::GetTextContent() const {
    return parsedData.textContent;
}

const TCRParsedData& TCREngine::GetParsedData() const {
    return parsedData;
}

float TCREngine::GetCompressionRatio() const {
    return parsedData.compressionRatio;
}

size_t TCREngine::GetCompressedSize() const {
    return parsedData.compressedSize;
}

size_t TCREngine::GetUncompressedSize() const {
    return parsedData.uncompressedSize;
}

const TCRDictionaryEntry& TCREngine::GetDictionaryEntry(uint8_t index) const {
    return parsedData.dictionary[index];
}

// ============================================================================
// COVER IMAGE (NOT SUPPORTED)
// ============================================================================

std::vector<uint8_t> TCREngine::GetCoverImage() const {
    // TCR format doesn't support images
    return std::vector<uint8_t>();
}

// ============================================================================
// SEARCH
// ============================================================================

std::vector<EBookSearchResult> TCREngine::Search(const std::string& query) const {
    std::vector<EBookSearchResult> results;
    
    if (query.empty() || parsedData.textContent.empty()) {
        return results;
    }
    
    // Simple case-insensitive search
    std::string lowerContent = parsedData.textContent;
    std::string lowerQuery = query;
    std::transform(lowerContent.begin(), lowerContent.end(), 
                   lowerContent.begin(), ::tolower);
    std::transform(lowerQuery.begin(), lowerQuery.end(),
                   lowerQuery.begin(), ::tolower);
    
    size_t pos = 0;
    while ((pos = lowerContent.find(lowerQuery, pos)) != std::string::npos) {
        EBookSearchResult result;
        result.matchedText = parsedData.textContent.substr(pos, query.length());
        
        // Extract context (50 chars before and after)
        size_t contextStart = (pos > 50) ? pos - 50 : 0;
        size_t contextEnd = std::min(pos + query.length() + 50, 
                                     parsedData.textContent.length());
        
        result.contextBefore = parsedData.textContent.substr(contextStart, pos - contextStart);
        result.contextAfter = parsedData.textContent.substr(
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

std::string TCREngine::GetLastError() const {
    return lastError;
}

void TCREngine::SetProgressCallback(std::function<void(float, const std::string&)> callback) {
    progressCallback = callback;
}

void TCREngine::ReportProgress(float progress, const std::string& status) {
    if (progressCallback) {
        progressCallback(progress, status);
    }
}

// ============================================================================
// TCR DECOMPRESSOR IMPLEMENTATION
// ============================================================================

bool TCRDecompressor::Initialize(const std::array<TCRDictionaryEntry, TCR_DICTIONARY_SIZE>& dict) {
    dictionary = dict;
    initialized = true;
    return true;
}

std::string TCRDecompressor::Decompress(const uint8_t* data, size_t size) {
    if (!initialized) {
        return std::string();
    }
    
    std::string result;
    result.reserve(size * 2); // Estimate decompressed size
    
    for (size_t i = 0; i < size; ++i) {
        uint8_t byte = data[i];
        result += dictionary[byte].replacement;
    }
    
    return result;
}

std::string TCRDecompressor::DecompressByte(uint8_t byte) const {
    if (!initialized) {
        return std::string(1, static_cast<char>(byte));
    }
    return dictionary[byte].replacement;
}

// ============================================================================
// TCR COMPRESSOR IMPLEMENTATION
// ============================================================================

std::vector<uint8_t> TCRCompressor::Compress(const std::string& text) {
    std::vector<uint8_t> result;
    
    // Write magic
    result.insert(result.end(), TCR_MAGIC, TCR_MAGIC + TCR_MAGIC_LENGTH);
    
    // Build dictionary
    auto dictionary = BuildDictionary(text);
    
    // Write dictionary
    for (int i = 0; i < 256; ++i) {
        const std::string& entry = dictionary[i].replacement;
        
        // Write length byte
        uint8_t len = static_cast<uint8_t>(std::min(entry.length(), size_t(255)));
        result.push_back(len);
        
        // Write replacement string
        result.insert(result.end(), entry.begin(), entry.begin() + len);
    }
    
    // Build reverse lookup
    std::map<std::string, uint8_t> reverseLookup;
    for (int i = 0; i < 256; ++i) {
        if (!dictionary[i].replacement.empty()) {
            reverseLookup[dictionary[i].replacement] = static_cast<uint8_t>(i);
        }
    }
    
    // Compress text
    size_t pos = 0;
    while (pos < text.length()) {
        bool found = false;
        
        // Try to find longest matching dictionary entry
        for (size_t len = std::min(size_t(16), text.length() - pos); len >= 1; --len) {
            std::string substr = text.substr(pos, len);
            auto it = reverseLookup.find(substr);
            
            if (it != reverseLookup.end()) {
                result.push_back(it->second);
                pos += len;
                found = true;
                break;
            }
        }
        
        if (!found) {
            // No match, output raw byte
            result.push_back(static_cast<uint8_t>(text[pos]));
            ++pos;
        }
    }
    
    return result;
}

std::array<TCRDictionaryEntry, TCR_DICTIONARY_SIZE> TCRCompressor::BuildDictionary(
    const std::string& text) {
    std::array<TCRDictionaryEntry, TCR_DICTIONARY_SIZE> dictionary;
    
    // Initialize with single characters (0-127 map to themselves)
    for (int i = 0; i < 128; ++i) {
        dictionary[i].replacement = std::string(1, static_cast<char>(i));
    }
    
    // Use symbols 128-255 for common pairs
    // Find most common character pairs in text
    std::map<std::pair<char, char>, int> pairCounts;
    for (size_t i = 0; i + 1 < text.length(); ++i) {
        pairCounts[{text[i], text[i+1]}]++;
    }
    
    // Sort by frequency
    std::vector<std::pair<std::pair<char, char>, int>> sortedPairs(
        pairCounts.begin(), pairCounts.end());
    std::sort(sortedPairs.begin(), sortedPairs.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    // Assign top 128 pairs to symbols 128-255
    int symbolIndex = 128;
    for (const auto& pair : sortedPairs) {
        if (symbolIndex >= 256) break;
        if (pair.second < 2) break; // Don't bother with rare pairs
        
        dictionary[symbolIndex].replacement = 
            std::string{pair.first.first, pair.first.second};
        ++symbolIndex;
    }
    
    // Fill remaining symbols with single characters
    for (int i = symbolIndex; i < 256; ++i) {
        dictionary[i].replacement = std::string(1, static_cast<char>(i));
    }
    
    return dictionary;
}

std::pair<char, char> TCRCompressor::FindMostCommonPair(const std::string& text) {
    std::map<std::pair<char, char>, int> pairCounts;
    
    for (size_t i = 0; i + 1 < text.length(); ++i) {
        pairCounts[{text[i], text[i+1]}]++;
    }
    
    std::pair<char, char> mostCommon = {0, 0};
    int maxCount = 0;
    
    for (const auto& pair : pairCounts) {
        if (pair.second > maxCount) {
            maxCount = pair.second;
            mostCommon = pair.first;
        }
    }
    
    return mostCommon;
}

std::string TCRCompressor::ReplacePair(const std::string& text,
                                        char first, char second, char symbol) {
    std::string result;
    result.reserve(text.length());
    
    for (size_t i = 0; i < text.length(); ++i) {
        if (i + 1 < text.length() && text[i] == first && text[i+1] == second) {
            result.push_back(symbol);
            ++i; // Skip second character
        } else {
            result.push_back(text[i]);
        }
    }
    
    return result;
}

// ============================================================================
// TCR DETECTION UTILITIES
// ============================================================================

namespace TCRDetection {

bool HasTCRExtension(const std::string& filePath) {
    size_t dotPos = filePath.find_last_of('.');
    if (dotPos == std::string::npos) {
        return false;
    }
    
    std::string ext = filePath.substr(dotPos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    return ext == "tcr";
}

bool HasTCRMagic(const uint8_t* data, size_t size) {
    if (size < TCR_MAGIC_LENGTH) {
        return false;
    }
    
    return std::memcmp(data, TCR_MAGIC, TCR_MAGIC_LENGTH) == 0;
}

std::string GetMagicString() {
    return std::string(TCR_MAGIC);
}

} // namespace TCRDetection

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

std::unique_ptr<IEBookEngine> CreateTCREngine() {
    return std::make_unique<TCREngine>();
}

void RegisterTCRPlugin() {
    // Register with EBookEngineFactory
    // EBookEngineFactory::RegisterEngine("tcr", CreateTCREngine);
}

void UnregisterTCRPlugin() {
    // Unregister from EBookEngineFactory
    // EBookEngineFactory::UnregisterEngine("tcr");
}

} // namespace UltraCanvas
