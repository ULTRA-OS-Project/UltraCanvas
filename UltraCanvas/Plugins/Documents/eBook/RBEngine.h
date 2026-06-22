// Plugins/Documents/eBook/RBEngine.h
// RocketEdition (NuvoMedia/Gemstar) RB eBook Engine
// Version: 1.0.0
// Last Modified: 2025-01-10
// Author: UltraCanvas Framework
#pragma once

#include "IEBookEngine.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>

namespace UltraCanvas {

// ============================================================================
// RB FORMAT INFORMATION
// ============================================================================
//
// RB (RocketEdition/Rocket eBook) is an eBook format developed by NuvoMedia
// for the Rocket eBook device (1998). Later used by Gemstar REB 1100/GEB 1150.
// One of the first dedicated eBook reader formats.
//
// File Extension: .rb
// Magic Signature: 0xB00CB00C ("bookbook") for eBooks
//                  0xB00CC0DE ("bookcode") for firmware
//                  0xB00CF00D ("bookfood") for system files
//
// Structure:
// ┌─────────────────────────────────────────────────────────────┐
// │ Header (0x00 - 0x127)                                       │
// │   - Magic: B0 0C B0 0C (4 bytes)                            │
// │   - Version: 02 00 (2 bytes)                                │
// │   - Creator: "NUVO" (4 bytes)                               │
// │   - Reserved: 00 00 00 00 (4 bytes)                         │
// │   - Date: year(2) + month(1) + day(1) (4 bytes)             │
// │   - Reserved: 6 bytes                                       │
// │   - TOC Offset: int32 (typically 0x128)                     │
// │   - File Length: int32                                      │
// │   - DRM Data: 0x20-0x127 (zeros if unencrypted)             │
// ├─────────────────────────────────────────────────────────────┤
// │ Table of Contents (typically at 0x128)                      │
// │   - Entry Count: int32                                      │
// │   - Entries[]:                                              │
// │     - Name: 32 bytes (zero-padded)                          │
// │     - Length: int32                                         │
// │     - Offset: int32                                         │
// │     - Flags: int32 (1=encrypted, 2=info, 8=deflated)        │
// ├─────────────────────────────────────────────────────────────┤
// │ Content Pages                                               │
// │   - HTML content (.html)                                    │
// │   - HTML index (.hidx)                                      │
// │   - Images (.png)                                           │
// │   - Info page (.info)                                       │
// │   - Keyword index (.hkey)                                   │
// ├─────────────────────────────────────────────────────────────┤
// │ Padding (20 bytes of 0x01)                                  │
// └─────────────────────────────────────────────────────────────┘
//
// Compression: DEFLATE (zlib) with window-bits=13, best compression
// Content: Simplified HTML, PNG images, metadata
//
// ============================================================================

// ===== RB MAGIC SIGNATURES =====
constexpr uint32_t RB_MAGIC_BOOK = 0x0CB00CB0;     // "bookbook" (little-endian)
constexpr uint32_t RB_MAGIC_CODE = 0xDEC00CB0;     // "bookcode" (firmware)
constexpr uint32_t RB_MAGIC_FOOD = 0x0DF00CB0;     // "bookfood" (system)

// Alternative representation for checking
constexpr uint8_t RB_MAGIC_BYTES[] = { 0xB0, 0x0C, 0xB0, 0x0C };
constexpr size_t RB_MAGIC_LENGTH = 4;

// ===== RB HEADER SIZES =====
constexpr size_t RB_HEADER_SIZE = 0x128;          // 296 bytes
constexpr size_t RB_DEFAULT_TOC_OFFSET = 0x128;

// ===== RB TOC ENTRY SIZE =====
constexpr size_t RB_TOC_NAME_SIZE = 32;
constexpr size_t RB_TOC_ENTRY_SIZE = 32 + 12;     // Name + 3 int32s

// ===== RB FLAGS =====
namespace RBFlags {
    constexpr uint32_t Encrypted = 0x01;          // DRM encrypted
    constexpr uint32_t Info = 0x02;               // Info page (metadata)
    constexpr uint32_t Deflated = 0x08;           // DEFLATE compressed
}

// ===== RB HEADER STRUCTURE =====
#pragma pack(push, 1)
struct RBHeader {
    uint32_t magic;                 // 0x00: Magic signature
    uint16_t versionMajor;          // 0x04: Major version (typically 2)
    uint16_t versionMinor;          // 0x06: Minor version (typically 0)
    char creator[4];                // 0x08: "NUVO" or zeros
    uint32_t reserved1;             // 0x0C: Reserved (zeros)
    uint16_t year;                  // 0x10: Creation year
    uint8_t month;                  // 0x12: Creation month (1-12)
    uint8_t day;                    // 0x13: Creation day (1-31)
    uint8_t reserved2[6];           // 0x14: Reserved (zeros)
    uint32_t tocOffset;             // 0x18: Offset to TOC
    uint32_t fileLength;            // 0x1C: Total file length
    // 0x20-0x127: DRM data (zeros if unencrypted)
};
#pragma pack(pop)

// ===== RB TOC ENTRY =====
struct RBTOCEntry {
    std::string name;               // Page name (e.g., "000001-chapter.html")
    uint32_t length;                // Data length
    uint32_t offset;                // Absolute offset in file
    uint32_t flags;                 // Entry flags
    
    // Helper methods
    bool IsEncrypted() const { return (flags & RBFlags::Encrypted) != 0; }
    bool IsInfo() const { return (flags & RBFlags::Info) != 0; }
    bool IsDeflated() const { return (flags & RBFlags::Deflated) != 0; }
    
    std::string GetExtension() const;
    std::string GetBaseName() const;
};

// ===== RB INFO PAGE (METADATA) =====
struct RBInfoPage {
    std::string title;
    std::string author;
    std::string comment;
    std::string url;
    std::string generator;
    std::string bodyPage;           // Root HTML page name
    std::string menuMark;
    std::string isbn;
    std::string publisher;
    std::string copyright;
    std::string genre;
    std::string revision;
    std::string language;
    int type = 0;
    bool hasHKey = false;           // Has keyword index
};

// ===== RB CONTENT PAGE =====
struct RBContentPage {
    std::string name;               // Original name from TOC
    std::string mimeType;           // MIME type
    std::vector<uint8_t> data;      // Decompressed content
    bool wasCompressed;             // Was deflated
    bool wasEncrypted;              // Was encrypted (not supported)
};

// ===== RB HTML INDEX (.hidx) =====
struct RBHtmlIndex {
    // Tag definitions
    struct TagDef {
        std::string tag;            // HTML tag (e.g., "<P ALIGN=\"center\">")
        int parentIndex;            // Parent tag index (-1 for root)
    };
    std::vector<TagDef> tags;
    
    // Paragraph positions
    struct ParagraphEntry {
        uint32_t offset;            // Character offset in HTML
        int tagIndex;               // Tag definition index
    };
    std::vector<ParagraphEntry> paragraphs;
    
    // Anchor names
    struct AnchorEntry {
        std::string name;           // Anchor name
        uint32_t offset;            // Character offset in HTML
    };
    std::vector<AnchorEntry> anchors;
};

// ===== RB KEYWORD INDEX (.hkey) =====
struct RBKeywordEntry {
    std::string word;               // Keyword (ASCII normalized)
    uint32_t offset;                // Offset in associated HTML
};

// ===== RB PARSED DATA =====
struct RBParsedData {
    // Header
    RBHeader header;
    uint16_t version;
    
    // TOC
    std::vector<RBTOCEntry> toc;
    
    // Content pages
    std::map<std::string, RBContentPage> pages;
    
    // Info page (metadata)
    RBInfoPage info;
    
    // HTML indices
    std::map<std::string, RBHtmlIndex> htmlIndices;
    
    // Keyword indices
    std::map<std::string, std::vector<RBKeywordEntry>> keywords;
    
    // Full text content
    std::string fullTextContent;
    
    // State
    bool hasValidHeader = false;
    bool hasEncryptedContent = false;
    std::string creationDate;
};

// ============================================================================
// RB ENGINE CLASS
// ============================================================================

class RBEngine : public IEBookEngine {
public:
    // ===== CONSTRUCTOR & DESTRUCTOR =====
    RBEngine();
    virtual ~RBEngine();
    
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
    
    // Cover image (RB doesn't have dedicated covers)
    std::vector<uint8_t> GetCoverImage() const override;
    
    // Error handling
    std::string GetLastError() const override;
    
    // Progress callback
    void SetProgressCallback(std::function<void(float, const std::string&)> callback) override;
    
    // ===== RB-SPECIFIC METHODS =====
    
    // Get parsed data
    const RBParsedData& GetParsedData() const;
    
    // Check if file is RB format
    static bool IsRBFile(const std::string& filePath);
    static bool IsRBData(const uint8_t* data, size_t size);
    
    // Get info page
    const RBInfoPage& GetInfoPage() const;
    
    // Get content page
    const RBContentPage* GetContentPage(const std::string& name) const;
    
    // Get all page names
    std::vector<std::string> GetPageNames() const;
    
    // Get HTML pages only
    std::vector<std::string> GetHTMLPageNames() const;
    
    // Get creation date
    std::string GetCreationDate() const;
    
    // Check for encryption
    bool HasEncryptedContent() const;
    
protected:
    // ===== PARSING METHODS =====
    
    // Parse RB file
    bool ParseRB(const uint8_t* data, size_t size);
    
    // Validate header
    bool ValidateHeader(const uint8_t* data, size_t size);
    
    // Parse header
    bool ParseHeader(const uint8_t* data, size_t size);
    
    // Parse TOC
    bool ParseTOC(const uint8_t* data, size_t size);
    
    // Parse content pages
    bool ParseContentPages(const uint8_t* data, size_t size);
    
    // Extract single page
    bool ExtractPage(const uint8_t* data, size_t size,
                     const RBTOCEntry& entry, RBContentPage& page);
    
    // Decompress deflated data
    std::vector<uint8_t> DecompressDeflate(const uint8_t* data, size_t size);
    
    // Parse info page
    bool ParseInfoPage(const std::string& content);
    
    // Parse HTML index (.hidx)
    bool ParseHtmlIndex(const std::string& name, const std::string& content);
    
    // Parse keyword index (.hkey)
    bool ParseKeywordIndex(const std::string& name, const std::string& content);
    
    // Extract text from HTML
    std::string ExtractTextFromHTML(const std::string& html);
    
    // Build page structure
    void BuildPageStructure();
    
    // Build table of contents
    void BuildTableOfContents();
    
private:
    // ===== STATE =====
    RBParsedData parsedData;
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
    
    // Read little-endian values
    static uint16_t ReadLE16(const uint8_t* data);
    static uint32_t ReadLE32(const uint8_t* data);
    
    // Report progress
    void ReportProgress(float progress, const std::string& status);
    
    // Determine MIME type from name
    std::string GetMimeType(const std::string& name) const;
};

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

// Create RB engine instance
std::unique_ptr<IEBookEngine> CreateRBEngine();

// Register RB format with engine factory
void RegisterRBPlugin();
void UnregisterRBPlugin();

// ============================================================================
// RB DETECTION UTILITIES
// ============================================================================

namespace RBDetection {
    // Check if file has RB extension
    bool HasRBExtension(const std::string& filePath);
    
    // Check if data has RB magic
    bool HasRBMagic(const uint8_t* data, size_t size);
    
    // Get magic type
    enum class RBType { Unknown, Book, Code, Food };
    RBType GetRBType(const uint8_t* data, size_t size);
    
    // Check for encrypted content
    bool HasEncryption(const uint8_t* data, size_t size);
}

} // namespace UltraCanvas

/*
=============================================================================
RB ENGINE USAGE EXAMPLES
=============================================================================

// Example 1: Load and read RB file
auto rbEngine = UltraCanvas::CreateRBEngine();
if (rbEngine->LoadFile("/path/to/book.rb")) {
    auto metadata = rbEngine->GetMetadata();
    std::cout << "Title: " << metadata.title << std::endl;
    std::cout << "Author: " << metadata.author << std::endl;
    
    auto* engine = static_cast<UltraCanvas::RBEngine*>(rbEngine.get());
    std::cout << "Created: " << engine->GetCreationDate() << std::endl;
}

// Example 2: Get info page details
auto rbEngine = UltraCanvas::CreateRBEngine();
rbEngine->LoadFile("/path/to/book.rb");
auto* engine = static_cast<UltraCanvas::RBEngine*>(rbEngine.get());

const auto& info = engine->GetInfoPage();
std::cout << "Generator: " << info.generator << std::endl;
std::cout << "Body page: " << info.bodyPage << std::endl;
std::cout << "Genre: " << info.genre << std::endl;

// Example 3: List all pages
auto* engine = static_cast<UltraCanvas::RBEngine*>(rbEngine.get());
auto htmlPages = engine->GetHTMLPageNames();
for (const auto& name : htmlPages) {
    std::cout << "HTML page: " << name << std::endl;
    auto* page = engine->GetContentPage(name);
    if (page) {
        std::cout << "  Size: " << page->data.size() << std::endl;
    }
}

// Example 4: Search content
auto rbEngine = UltraCanvas::CreateRBEngine();
rbEngine->LoadFile("/path/to/book.rb");
auto results = rbEngine->Search("adventure");
for (const auto& result : results) {
    std::cout << "Found on page " << result.pageNumber << std::endl;
}

=============================================================================
RB FORMAT SUPPORT MATRIX
=============================================================================

| Feature                    | Support | Notes                           |
|---------------------------|---------|----------------------------------|
| Header Parsing            | ✅      | Full header support              |
| TOC Parsing               | ✅      | All entry types                  |
| DEFLATE Decompression     | ✅      | Via separate module              |
| HTML Content              | ✅      | Simplified HTML                  |
| PNG Images                | ✅      | B&W images                       |
| Info Page (Metadata)      | ✅      | All NAME=VALUE pairs             |
| HTML Index (.hidx)        | ✅      | Tags, paragraphs, anchors        |
| Keyword Index (.hkey)     | ✅      | Word lookup                      |
| DRM/Encryption            | ❌      | Not supported (legal)            |
| Search                    | ✅      | Full-text search                 |
| Version 2.x               | ✅      | Standard version                 |

Legend: ✅ Supported | ⚠️ Partial | ❌ Not supported

=============================================================================
*/
