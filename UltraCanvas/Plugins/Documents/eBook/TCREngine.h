// Plugins/Documents/eBook/TCREngine.h
// Psion Series 3 TCR (Text Compression for Reader) eBook Engine
// Version: 1.0.0
// Last Modified: 2025-01-10
// Author: UltraCanvas Framework
#pragma once

#include "IEBookEngine.h"
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <cstdint>

namespace UltraCanvas {

// ============================================================================
// TCR FORMAT INFORMATION
// ============================================================================
//
// TCR (Text Compression for Reader) is a legacy eBook format developed for
// the Psion Series 3 palmtop devices in the early 1990s. It uses ZVR-based
// dictionary compression to achieve approximately 50% compression ratios.
//
// File Extension: .tcr
// Magic Bytes: "!!8-Bit!!" (0x21 0x21 0x38 0x2D 0x42 0x69 0x74 0x21 0x21)
//
// Structure:
// 1. Magic signature: "!!8-Bit!!" (9 bytes)
// 2. Dictionary: 256 entries, each a replacement string
// 3. Compressed data: Each byte maps to dictionary entry
//
// Compression Algorithm (ZVR):
// - Dictionary-based replacement compression
// - 256 possible symbols (0x00-0xFF)
// - Each symbol maps to a replacement string
// - Decompression: Read byte -> output dictionary[byte]
// - Very fast decompression, even on limited hardware
//
// Features:
// - Plain text only (no formatting)
// - No metadata support
// - Simple structure, fast decompression
// - ~50% compression ratio for English text
//
// ============================================================================

// ===== TCR MAGIC SIGNATURE =====
constexpr const char* TCR_MAGIC = "!!8-Bit!!";
constexpr size_t TCR_MAGIC_LENGTH = 9;

// ===== TCR DICTIONARY SIZE =====
constexpr size_t TCR_DICTIONARY_SIZE = 256;

// ===== TCR DICTIONARY ENTRY =====
struct TCRDictionaryEntry {
    std::string replacement;    // Replacement string for this symbol
    
    TCRDictionaryEntry() = default;
    TCRDictionaryEntry(const std::string& str) : replacement(str) {}
};

// ===== TCR PARSED DATA =====
struct TCRParsedData {
    // Dictionary
    std::array<TCRDictionaryEntry, TCR_DICTIONARY_SIZE> dictionary;
    
    // Compressed data offset (after dictionary)
    size_t compressedDataOffset = 0;
    
    // Decompressed text content
    std::string textContent;
    
    // Statistics
    size_t compressedSize = 0;
    size_t uncompressedSize = 0;
    float compressionRatio = 0.0f;
    
    // File info
    bool hasValidMagic = false;
};

// ===== TCR CONTROL CODES =====
// TCR files may use special control codes for formatting hints
namespace TCRControlCode {
    constexpr char PARAGRAPH_BREAK = '\n';      // Paragraph separator
    constexpr char PAGE_BREAK = '\f';           // Form feed (page break)
    constexpr char CHAPTER_MARK = '\x1F';       // Unit separator (chapter)
}

// ============================================================================
// TCR ENGINE CLASS
// ============================================================================

class TCREngine : public IEBookEngine {
public:
    // ===== CONSTRUCTOR & DESTRUCTOR =====
    TCREngine();
    virtual ~TCREngine();
    
    // ===== IEBookEngine INTERFACE =====
    
    // Format identification
    EBookFormat GetFormat() const override;
    std::string GetFormatName() const override;
    std::vector<std::string> GetSupportedExtensions() const override;
    
    // Document loading
    bool LoadFile(const std::string& filePath,
                  const std::string& password = "") override;
    bool LoadFromMemory(const std::vector<uint8_t>& data,
                        const std::string& password = "") override;
    void Close() override;
    bool IsLoaded() const override;
    
    // Validation
    bool ValidateFile(const std::string& filePath) override;
    
    // Metadata
    EBookMetadata GetMetadata() const override;
    
    // Content access
    int GetPageCount() const override;
    EBookPageInfo GetPageInfo(int pageNumber) const override;
    std::vector<EBookTOCEntry> GetTableOfContents() const override;
    std::string GetPageContent(int pageNumber) const override;
    std::vector<uint8_t> RenderPageToImage(int pageNumber,
                                            int width, int height,
                                            float dpi) const override;
    
    // Search
    std::vector<EBookSearchResult> Search(const std::string& query) const override;
    
    // Cover image (not supported for TCR)
    std::vector<uint8_t> GetCoverImage() const override;
    
    // Error handling
    std::string GetLastError() const override;
    
    // Progress callback
    void SetProgressCallback(std::function<void(float, const std::string&)> callback) override;
    
    // ===== TCR-SPECIFIC METHODS =====
    
    // Get parsed data
    const TCRParsedData& GetParsedData() const;
    
    // Check if file is TCR format
    static bool IsTCRFile(const std::string& filePath);
    static bool IsTCRData(const uint8_t* data, size_t size);
    
    // Get raw text content
    std::string GetTextContent() const;
    
    // Get compression statistics
    float GetCompressionRatio() const;
    size_t GetCompressedSize() const;
    size_t GetUncompressedSize() const;
    
    // Get dictionary entry
    const TCRDictionaryEntry& GetDictionaryEntry(uint8_t index) const;
    
protected:
    // ===== PARSING METHODS =====
    
    // Parse TCR file structure
    bool ParseTCR(const uint8_t* data, size_t size);
    
    // Validate magic signature
    bool ValidateMagic(const uint8_t* data, size_t size);
    
    // Parse dictionary section
    bool ParseDictionary(const uint8_t* data, size_t size, size_t& offset);
    
    // Decompress content
    bool DecompressContent(const uint8_t* data, size_t size, size_t offset);
    
    // Build metadata from content
    void BuildMetadata();
    
    // Build table of contents from content
    void BuildTableOfContents();
    
    // Calculate pages from content
    void CalculatePages();
    
private:
    // ===== STATE =====
    TCRParsedData parsedData;
    EBookMetadata metadata;
    std::vector<EBookTOCEntry> tableOfContents;
    std::vector<EBookPageInfo> pageInfos;
    
    bool isLoaded = false;
    std::string lastError;
    std::string currentFilePath;
    
    int pageCount = 0;
    
    // Progress callback
    std::function<void(float, const std::string&)> progressCallback;
    
    // ===== HELPERS =====
    
    // Extract title from filename
    std::string ExtractTitleFromPath(const std::string& filePath) const;
    
    // Report progress
    void ReportProgress(float progress, const std::string& status);
};

// ============================================================================
// TCR DECOMPRESSOR
// ============================================================================

class TCRDecompressor {
public:
    // Initialize with dictionary
    bool Initialize(const std::array<TCRDictionaryEntry, TCR_DICTIONARY_SIZE>& dictionary);
    
    // Decompress data
    std::string Decompress(const uint8_t* data, size_t size);
    
    // Decompress single byte
    std::string DecompressByte(uint8_t byte) const;
    
    bool IsInitialized() const { return initialized; }
    
private:
    bool initialized = false;
    std::array<TCRDictionaryEntry, TCR_DICTIONARY_SIZE> dictionary;
};

// ============================================================================
// TCR COMPRESSOR (for creating TCR files)
// ============================================================================

class TCRCompressor {
public:
    // Compress text to TCR format
    static std::vector<uint8_t> Compress(const std::string& text);
    
    // Build optimal dictionary from text
    static std::array<TCRDictionaryEntry, TCR_DICTIONARY_SIZE> BuildDictionary(
        const std::string& text);
    
private:
    // Find most common symbol pair
    static std::pair<char, char> FindMostCommonPair(const std::string& text);
    
    // Replace pair with symbol
    static std::string ReplacePair(const std::string& text, 
                                    char first, char second, char symbol);
};

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

// Create TCR engine instance
std::unique_ptr<IEBookEngine> CreateTCREngine();

// Register TCR format with engine factory
void RegisterTCRPlugin();
void UnregisterTCRPlugin();

// ============================================================================
// TCR DETECTION UTILITIES
// ============================================================================

namespace TCRDetection {
    // Check if file has TCR extension
    bool HasTCRExtension(const std::string& filePath);
    
    // Check if data has TCR magic
    bool HasTCRMagic(const uint8_t* data, size_t size);
    
    // Get magic string
    std::string GetMagicString();
}

} // namespace UltraCanvas

/*
=============================================================================
TCR ENGINE USAGE EXAMPLES
=============================================================================

// Example 1: Load and read TCR file
auto tcrEngine = UltraCanvas::CreateTCREngine();
if (tcrEngine->LoadFile("/path/to/book.tcr")) {
    // Get text content
    auto* engine = static_cast<UltraCanvas::TCREngine*>(tcrEngine.get());
    std::string text = engine->GetTextContent();
    std::cout << "Content length: " << text.length() << std::endl;
    
    // Get compression stats
    std::cout << "Compression ratio: " << engine->GetCompressionRatio() << "%" << std::endl;
}

// Example 2: Search within TCR book
auto tcrEngine = UltraCanvas::CreateTCREngine();
tcrEngine->LoadFile("/path/to/book.tcr");

auto results = tcrEngine->Search("adventure");
for (const auto& result : results) {
    std::cout << "Found on page " << result.pageNumber << std::endl;
    std::cout << "Context: " << result.contextBefore 
              << "[" << result.matchedText << "]" 
              << result.contextAfter << std::endl;
}

// Example 3: Use with EBookViewer
auto viewer = UltraCanvas::CreateEBookViewer("viewer", 0, 0, 800, 600);
viewer->LoadDocument("/path/to/book.tcr");
// Viewer handles TCR files automatically

// Example 4: Check file type before loading
if (UltraCanvas::TCREngine::IsTCRFile("/path/to/file.tcr")) {
    auto engine = UltraCanvas::CreateTCREngine();
    engine->LoadFile("/path/to/file.tcr");
} else {
    std::cout << "Not a TCR file" << std::endl;
}

// Example 5: Create TCR file from text
std::string text = "This is the content of my book...";
std::vector<uint8_t> tcrData = UltraCanvas::TCRCompressor::Compress(text);
// Save tcrData to file

=============================================================================
TCR FORMAT SUPPORT MATRIX
=============================================================================

| Feature                    | Support | Notes                           |
|---------------------------|---------|----------------------------------|
| ZVR Decompression         | ✅      | Full dictionary support          |
| Plain Text Content        | ✅      | ASCII text                       |
| Unicode Text              | ⚠️      | Limited (depends on source)      |
| Paragraph Breaks          | ✅      | Via newlines                     |
| Page Breaks               | ✅      | Via form feed                    |
| Chapter Marks             | ⚠️      | Limited support                  |
| Metadata                  | ❌      | Not supported by format          |
| Images                    | ❌      | Not supported by format          |
| Formatting                | ❌      | Plain text only                  |
| Search                    | ✅      | Full-text search                 |
| Compression               | ✅      | Can create TCR files             |

Legend: ✅ Supported | ⚠️ Partial | ❌ Not supported

=============================================================================
*/
