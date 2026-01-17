// Plugins/Documents/eBook/MOBIEngine.h
// Mobipocket (MOBI/PRC) eBook Engine
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
// MOBI FORMAT INFORMATION
// ============================================================================
//
// Mobipocket (MOBI) is a proprietary eBook format developed by Mobipocket SA
// in 2000, later acquired by Amazon in 2005. It is based on the Palm Database
// (PDB) format with additional headers for eBook-specific functionality.
//
// File Extensions: .mobi, .prc, .pdb
// MIME Type: application/x-mobipocket-ebook
//
// Structure:
// 1. PDB Header (78 bytes) - Palm Database metadata
// 2. Record List - Offsets to all records
// 3. Record 0:
//    - PalmDOC Header (16 bytes)
//    - MOBI Header (variable, starts with "MOBI")
//    - EXTH Header (optional, extended metadata)
//    - Full book name
// 4. Text Records - Compressed book content
// 5. Image Records - Embedded images (JPEG, GIF, PNG)
// 6. Index Records - Optional navigation indices
//
// Compression Types:
// - 1: No compression
// - 2: PalmDOC (LZ77-based)
// - 17480 ('DH'): HUFF/CDIC (Huffman dictionary)
//
// ============================================================================

// ===== PDB HEADER STRUCTURE (78 bytes) =====
#pragma pack(push, 1)
struct PDBHeader {
    char name[32];              // Database name (null-terminated)
    uint16_t attributes;        // File attributes
    uint16_t version;           // Version
    uint32_t creationDate;      // Creation date (Palm epoch: 1904-01-01)
    uint32_t modificationDate;  // Modification date
    uint32_t lastBackupDate;    // Last backup date
    uint32_t modificationNumber;// Modification number
    uint32_t appInfoOffset;     // Offset to AppInfo (0 if none)
    uint32_t sortInfoOffset;    // Offset to SortInfo (0 if none)
    char type[4];               // Type ID (should be "BOOK")
    char creator[4];            // Creator ID (should be "MOBI")
    uint32_t uniqueIdSeed;      // Unique ID seed
    uint32_t nextRecordListId;  // Next record list ID (always 0)
    uint16_t recordCount;       // Number of records
};
#pragma pack(pop)

// ===== PDB RECORD ENTRY (8 bytes each) =====
#pragma pack(push, 1)
struct PDBRecordEntry {
    uint32_t offset;            // Offset to record data
    uint8_t attributes;         // Record attributes
    uint8_t uniqueId[3];        // 24-bit unique ID
};
#pragma pack(pop)

// ===== PALMDOC HEADER (16 bytes in Record 0) =====
#pragma pack(push, 1)
struct PalmDOCHeader {
    uint16_t compression;       // 1=none, 2=PalmDOC, 17480=HUFF/CDIC
    uint16_t unused;            // Always 0
    uint32_t textLength;        // Uncompressed text length
    uint16_t recordCount;       // Number of text records
    uint16_t recordSize;        // Max record size (4096)
    uint32_t currentPosition;   // Current reading position (or encryption type)
};
#pragma pack(pop)

// ===== MOBI HEADER (variable length, minimum 232 bytes) =====
#pragma pack(push, 1)
struct MOBIHeader {
    char identifier[4];         // "MOBI"
    uint32_t headerLength;      // Length of MOBI header
    uint32_t mobiType;          // Book type (2=Mobipocket Book, 3=PalmDoc, etc.)
    uint32_t textEncoding;      // 1252=CP1252, 65001=UTF-8
    uint32_t uniqueId;          // Unique ID
    uint32_t fileVersion;       // MOBI format version
    uint32_t ortographicIndex;  // Index to ortographic index (0xFFFFFFFF if none)
    uint32_t inflectionIndex;   // Index to inflection index
    uint32_t indexNames;        // Index to index names
    uint32_t indexKeys;         // Index to index keys
    uint32_t extraIndex[6];     // Additional indices
    uint32_t firstNonBookIndex; // First non-book record index
    uint32_t fullNameOffset;    // Offset to full name in record 0
    uint32_t fullNameLength;    // Length of full name
    uint32_t locale;            // Language locale
    uint32_t inputLanguage;     // Input language
    uint32_t outputLanguage;    // Output language
    uint32_t minVersion;        // Minimum reader version
    uint32_t firstImageIndex;   // First image record index
    uint32_t huffmanRecordOffset;// HUFF record index
    uint32_t huffmanRecordCount; // Number of HUFF records
    uint32_t huffmanTableOffset; // HUFF table offset
    uint32_t huffmanTableLength; // HUFF table length
    uint32_t exthFlags;         // EXTH flags (bit 6 = has EXTH)
    // Additional fields follow for longer headers...
};
#pragma pack(pop)

// ===== EXTH HEADER =====
#pragma pack(push, 1)
struct EXTHHeader {
    char identifier[4];         // "EXTH"
    uint32_t headerLength;      // Total EXTH header length
    uint32_t recordCount;       // Number of EXTH records
};
#pragma pack(pop)

// ===== EXTH RECORD =====
#pragma pack(push, 1)
struct EXTHRecord {
    uint32_t recordType;        // Record type
    uint32_t recordLength;      // Record length (including 8-byte header)
    // Data follows (recordLength - 8 bytes)
};
#pragma pack(pop)

// ===== EXTH RECORD TYPES =====
namespace EXTHRecordType {
    constexpr uint32_t DRM_SERVER_ID = 1;
    constexpr uint32_t DRM_COMMERCE_ID = 2;
    constexpr uint32_t DRM_EBOOKBASE_ID = 3;
    constexpr uint32_t AUTHOR = 100;
    constexpr uint32_t PUBLISHER = 101;
    constexpr uint32_t IMPRINT = 102;
    constexpr uint32_t DESCRIPTION = 103;
    constexpr uint32_t ISBN = 104;
    constexpr uint32_t SUBJECT = 105;
    constexpr uint32_t PUBLISHING_DATE = 106;
    constexpr uint32_t REVIEW = 107;
    constexpr uint32_t CONTRIBUTOR = 108;
    constexpr uint32_t RIGHTS = 109;
    constexpr uint32_t SUBJECT_CODE = 110;
    constexpr uint32_t TYPE = 111;
    constexpr uint32_t SOURCE = 112;
    constexpr uint32_t ASIN = 113;
    constexpr uint32_t VERSION_NUMBER = 114;
    constexpr uint32_t SAMPLE = 115;
    constexpr uint32_t START_READING = 116;
    constexpr uint32_t ADULT = 117;
    constexpr uint32_t RETAIL_PRICE = 118;
    constexpr uint32_t RETAIL_CURRENCY = 119;
    constexpr uint32_t KF8_BOUNDARY = 121;
    constexpr uint32_t RESOURCE_COUNT = 125;
    constexpr uint32_t KF8_COVER_URI = 129;
    constexpr uint32_t COVER_OFFSET = 201;      // Add to firstImageIndex
    constexpr uint32_t THUMB_OFFSET = 202;      // Add to firstImageIndex
    constexpr uint32_t FAKE_COVER = 203;
    constexpr uint32_t CREATOR_SOFTWARE = 204;
    constexpr uint32_t CREATOR_MAJOR = 205;
    constexpr uint32_t CREATOR_MINOR = 206;
    constexpr uint32_t CREATOR_BUILD = 207;
    constexpr uint32_t WATERMARK = 208;
    constexpr uint32_t TAMPER_KEYS = 209;
    constexpr uint32_t FONT_SIGNATURE = 300;
    constexpr uint32_t CLIPPING_LIMIT = 401;
    constexpr uint32_t PUBLISHER_LIMIT = 402;
    constexpr uint32_t CDETYPE = 501;           // PDOC, EBOK, EBSP
    constexpr uint32_t LAST_UPDATE_TIME = 502;
    constexpr uint32_t UPDATED_TITLE = 503;
    constexpr uint32_t ASIN_504 = 504;
    constexpr uint32_t LANGUAGE = 524;
}

// ===== MOBI COMPRESSION TYPES =====
enum class MOBICompression : uint16_t {
    None = 1,
    PalmDOC = 2,
    HuffCDIC = 17480   // 'DH'
};

// ===== MOBI BOOK TYPES =====
enum class MOBIBookType : uint32_t {
    MobipocketBook = 2,
    PalmDocBook = 3,
    Audio = 4,
    KindleGen1 = 232,
    KindleGen2 = 248,
    News = 257,
    NewsFeed = 258,
    NewsMagazine = 259,
    PICS = 513,
    WORD = 514,
    XLS = 515,
    PPT = 516,
    TEXT = 517,
    HTML = 518
};

// ===== MOBI TEXT ENCODING =====
enum class MOBIEncoding : uint32_t {
    CP1252 = 1252,      // Windows Latin-1
    UTF8 = 65001        // UTF-8
};

// ===== MOBI IMAGE RECORD =====
struct MOBIImageRecord {
    int recordIndex;
    std::vector<uint8_t> data;
    std::string mimeType;       // image/jpeg, image/gif, image/png
    bool isCover = false;
    bool isThumbnail = false;
};

// ===== MOBI PARSED DATA =====
struct MOBIParsedData {
    // Headers
    PDBHeader pdbHeader;
    PalmDOCHeader palmDocHeader;
    MOBIHeader mobiHeader;
    
    // Record info
    std::vector<PDBRecordEntry> recordEntries;
    std::vector<std::vector<uint8_t>> records;
    
    // Metadata
    std::string fullName;
    std::map<uint32_t, std::vector<std::string>> exthRecords;
    
    // Content
    std::string textContent;        // Decompressed text (HTML)
    std::vector<MOBIImageRecord> images;
    
    // Indices
    int coverImageIndex = -1;
    int thumbnailImageIndex = -1;
    int firstTextRecord = 1;
    int lastTextRecord = 0;
    
    // Flags
    bool hasEXTH = false;
    bool isEncrypted = false;
    bool isKF8 = false;
    MOBIEncoding encoding = MOBIEncoding::CP1252;
    MOBICompression compression = MOBICompression::PalmDOC;
};

// ============================================================================
// MOBI ENGINE CLASS
// ============================================================================

class MOBIEngine : public IEBookEngine {
public:
    // ===== CONSTRUCTOR & DESTRUCTOR =====
    MOBIEngine();
    virtual ~MOBIEngine();
    
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
    
    // Cover image
    std::vector<uint8_t> GetCoverImage() const override;
    
    // Error handling
    std::string GetLastError() const override;
    
    // Progress callback
    void SetProgressCallback(std::function<void(float, const std::string&)> callback) override;
    
    // ===== MOBI-SPECIFIC METHODS =====
    
    // Get parsed MOBI data
    const MOBIParsedData& GetParsedData() const;
    
    // Check if file is MOBI format
    static bool IsMOBIFile(const std::string& filePath);
    static bool IsMOBIData(const uint8_t* data, size_t size);
    
    // Get raw HTML content
    std::string GetHTMLContent() const;
    
    // Get image by index
    const MOBIImageRecord* GetImage(int index) const;
    int GetImageCount() const;
    
    // Check encryption status
    bool IsEncrypted() const;
    bool IsKF8Format() const;
    
protected:
    // ===== PARSING METHODS =====
    
    // Parse PDB structure
    bool ParsePDBHeader(const uint8_t* data, size_t size);
    bool ParseRecordList(const uint8_t* data, size_t size);
    bool ExtractRecords(const uint8_t* data, size_t size);
    
    // Parse MOBI structure (Record 0)
    bool ParseRecord0(const std::vector<uint8_t>& record0);
    bool ParsePalmDOCHeader(const uint8_t* data, size_t size);
    bool ParseMOBIHeader(const uint8_t* data, size_t size);
    bool ParseEXTHHeader(const uint8_t* data, size_t size);
    
    // Extract content
    bool ExtractTextContent();
    bool ExtractImages();
    
    // Decompression
    std::vector<uint8_t> DecompressPalmDOC(const uint8_t* data, size_t size);
    std::vector<uint8_t> DecompressHuffCDIC(const uint8_t* data, size_t size);
    
    // Build metadata from EXTH records
    void BuildMetadata();
    
    // Build table of contents from content
    void BuildTableOfContents();
    
    // Calculate pages from content
    void CalculatePages();
    
    // Convert text encoding
    std::string ConvertEncoding(const std::vector<uint8_t>& data) const;
    
private:
    // ===== STATE =====
    MOBIParsedData parsedData;
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
    
    // Byte order conversion (MOBI uses big-endian)
    static uint16_t ReadBE16(const uint8_t* data);
    static uint32_t ReadBE32(const uint8_t* data);
    
    // Get EXTH record as string
    std::string GetEXTHString(uint32_t recordType) const;
    std::vector<std::string> GetEXTHStrings(uint32_t recordType) const;
    uint32_t GetEXTHUInt32(uint32_t recordType) const;
    
    // Report progress
    void ReportProgress(float progress, const std::string& status);
};

// ============================================================================
// PALMDOC DECOMPRESSION
// ============================================================================

class PalmDOCDecompressor {
public:
    // Decompress PalmDOC LZ77 data
    static std::vector<uint8_t> Decompress(const uint8_t* data, size_t size,
                                            int trailingBytes = 0);
    
    // Compress data to PalmDOC format
    static std::vector<uint8_t> Compress(const uint8_t* data, size_t size);
    
private:
    // LZ77 decompression algorithm
    static void DecompressBlock(const uint8_t* input, size_t inputSize,
                                 std::vector<uint8_t>& output);
};

// ============================================================================
// HUFF/CDIC DECOMPRESSION (for newer MOBI files)
// ============================================================================

class HuffCDICDecompressor {
public:
    // Initialize with HUFF and CDIC records
    bool Initialize(const std::vector<std::vector<uint8_t>>& huffRecords,
                    const std::vector<std::vector<uint8_t>>& cdicRecords);
    
    // Decompress a text record
    std::vector<uint8_t> Decompress(const uint8_t* data, size_t size);
    
    bool IsInitialized() const { return initialized; }
    
private:
    bool initialized = false;
    
    // Huffman tables
    std::vector<uint32_t> dict1;
    std::vector<uint32_t> dict2;
    
    // CDIC data
    std::vector<std::vector<uint8_t>> cdicData;
    int cdicCount = 0;
    int codeLength = 0;
};

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

// Create MOBI engine instance
std::unique_ptr<IEBookEngine> CreateMOBIEngine();

// Register MOBI format with engine factory
void RegisterMOBIPlugin();
void UnregisterMOBIPlugin();

// ============================================================================
// MOBI DETECTION UTILITIES
// ============================================================================

namespace MOBIDetection {
    // Check if file has MOBI extension
    bool HasMOBIExtension(const std::string& filePath);
    
    // Check if data is PDB format
    bool IsPDBFormat(const uint8_t* data, size_t size);
    
    // Check if PDB is MOBI type
    bool IsMOBIType(const uint8_t* data, size_t size);
    
    // Get creator/type codes from PDB header
    std::string GetCreatorCode(const uint8_t* data, size_t size);
    std::string GetTypeCode(const uint8_t* data, size_t size);
}

} // namespace UltraCanvas

/*
=============================================================================
MOBI ENGINE USAGE EXAMPLES
=============================================================================

// Example 1: Load and read MOBI file
auto mobiEngine = UltraCanvas::CreateMOBIEngine();
if (mobiEngine->LoadFile("/path/to/book.mobi")) {
    auto metadata = mobiEngine->GetMetadata();
    std::cout << "Title: " << metadata.title << std::endl;
    std::cout << "Author: " << metadata.author << std::endl;
    std::cout << "Pages: " << mobiEngine->GetPageCount() << std::endl;
    
    // Get HTML content
    auto* mobi = static_cast<UltraCanvas::MOBIEngine*>(mobiEngine.get());
    std::string html = mobi->GetHTMLContent();
}

// Example 2: Extract cover image
auto mobiEngine = UltraCanvas::CreateMOBIEngine();
if (mobiEngine->LoadFile("/path/to/book.mobi")) {
    auto coverData = mobiEngine->GetCoverImage();
    if (!coverData.empty()) {
        // Save or display cover image
        std::ofstream cover("cover.jpg", std::ios::binary);
        cover.write(reinterpret_cast<char*>(coverData.data()), coverData.size());
    }
}

// Example 3: Check file type before loading
if (UltraCanvas::MOBIEngine::IsMOBIFile("/path/to/file.prc")) {
    auto engine = UltraCanvas::CreateMOBIEngine();
    engine->LoadFile("/path/to/file.prc");
} else if (UltraCanvas::EPUBEngine::IsEPUBFile("/path/to/file.epub")) {
    auto engine = UltraCanvas::CreateEPUBEngine();
    engine->LoadFile("/path/to/file.epub");
}

// Example 4: Search within book
auto mobiEngine = UltraCanvas::CreateMOBIEngine();
mobiEngine->LoadFile("/path/to/book.mobi");
auto results = mobiEngine->Search("adventure");
for (const auto& result : results) {
    std::cout << "Found on page " << result.pageNumber << std::endl;
    std::cout << "Context: " << result.contextBefore << "[" 
              << result.matchedText << "]" << result.contextAfter << std::endl;
}

=============================================================================
MOBI FORMAT SUPPORT MATRIX
=============================================================================

| Feature                    | Support | Notes                           |
|---------------------------|---------|----------------------------------|
| PalmDOC Compression       | ✅      | LZ77-based                       |
| HUFF/CDIC Compression     | ✅      | Huffman dictionary               |
| No Compression            | ✅      | Raw text                         |
| EXTH Metadata             | ✅      | Extended metadata records        |
| Cover Image               | ✅      | From EXTH cover offset           |
| Embedded Images           | ✅      | JPEG, GIF, PNG                   |
| UTF-8 Encoding            | ✅      | Encoding 65001                   |
| CP1252 Encoding           | ✅      | Encoding 1252                    |
| HTML Content              | ✅      | Formatted text                   |
| Table of Contents         | ✅      | Built from HTML structure        |
| Search                    | ✅      | Full-text search                 |
| DRM Encryption            | ❌      | Not supported (legal reasons)    |
| KF8 (AZW3) Dual Format    | ⚠️      | Partial (MOBI portion only)      |

Legend: ✅ Supported | ⚠️ Partial | ❌ Not supported

=============================================================================
*/
