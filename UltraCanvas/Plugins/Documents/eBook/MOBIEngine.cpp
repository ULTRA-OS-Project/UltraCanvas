// Plugins/Documents/eBook/MOBIEngine.cpp
// Mobipocket (MOBI/PRC) eBook Engine Implementation
// Version: 1.0.0
// Last Modified: 2025-01-10
// Author: UltraCanvas Framework

#include "MOBIEngine.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <cstring>
#include <codecvt>
#include <locale>

namespace UltraCanvas {

// ============================================================================
// BYTE ORDER HELPERS (MOBI uses Big-Endian)
// ============================================================================

uint16_t MOBIEngine::ReadBE16(const uint8_t* data) {
    return (static_cast<uint16_t>(data[0]) << 8) |
           static_cast<uint16_t>(data[1]);
}

uint32_t MOBIEngine::ReadBE32(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           static_cast<uint32_t>(data[3]);
}

// ============================================================================
// CONSTRUCTOR & DESTRUCTOR
// ============================================================================

MOBIEngine::MOBIEngine()
    : isLoaded(false)
    , pageCount(0) {
}

MOBIEngine::~MOBIEngine() {
    Close();
}

// ============================================================================
// FORMAT IDENTIFICATION
// ============================================================================

EBookFormat MOBIEngine::GetFormat() const {
    return EBookFormat::MOBI;
}

std::string MOBIEngine::GetFormatName() const {
    if (parsedData.isKF8) {
        return "Kindle Format 8 (KF8/AZW3)";
    }
    return "Mobipocket (MOBI)";
}

std::vector<std::string> MOBIEngine::GetSupportedExtensions() const {
    return { "mobi", "prc", "azw" };
}

// ============================================================================
// DOCUMENT LOADING
// ============================================================================

bool MOBIEngine::LoadFile(const std::string& filePath, const std::string& password) {
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
    if (fileSize < sizeof(PDBHeader)) {
        lastError = "File too small to be valid MOBI";
        return false;
    }
    
    file.seekg(0);
    std::vector<uint8_t> data(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);
    file.close();
    
    return LoadFromMemory(data, password);
}

bool MOBIEngine::LoadFromMemory(const std::vector<uint8_t>& data,
                                 const std::string& password) {
    Close();
    
    if (data.size() < sizeof(PDBHeader)) {
        lastError = "Data too small to be valid MOBI";
        return false;
    }
    
    ReportProgress(0.1f, "Parsing PDB header...");
    
    // Parse PDB header
    if (!ParsePDBHeader(data.data(), data.size())) {
        return false;
    }
    
    ReportProgress(0.2f, "Parsing record list...");
    
    // Parse record list
    if (!ParseRecordList(data.data(), data.size())) {
        return false;
    }
    
    ReportProgress(0.3f, "Extracting records...");
    
    // Extract all records
    if (!ExtractRecords(data.data(), data.size())) {
        return false;
    }
    
    ReportProgress(0.4f, "Parsing MOBI headers...");
    
    // Parse Record 0 (headers and metadata)
    if (!parsedData.records.empty()) {
        if (!ParseRecord0(parsedData.records[0])) {
            return false;
        }
    }
    
    // Check for encryption
    if (parsedData.isEncrypted) {
        if (password.empty()) {
            lastError = "Document is encrypted and no password provided";
            return false;
        }
        // DRM decryption not implemented for legal reasons
        lastError = "DRM-protected documents are not supported";
        return false;
    }
    
    ReportProgress(0.5f, "Extracting text content...");
    
    // Extract text content
    if (!ExtractTextContent()) {
        return false;
    }
    
    ReportProgress(0.7f, "Extracting images...");
    
    // Extract images
    ExtractImages();
    
    ReportProgress(0.8f, "Building metadata...");
    
    // Build metadata from EXTH records
    BuildMetadata();
    
    ReportProgress(0.9f, "Building table of contents...");
    
    // Build table of contents
    BuildTableOfContents();
    
    // Calculate pages
    CalculatePages();
    
    isLoaded = true;
    
    ReportProgress(1.0f, "Complete");
    
    return true;
}

void MOBIEngine::Close() {
    parsedData = MOBIParsedData();
    metadata = EBookMetadata();
    tableOfContents.clear();
    pageInfos.clear();
    isLoaded = false;
    pageCount = 0;
    lastError.clear();
}

bool MOBIEngine::IsLoaded() const {
    return isLoaded;
}

// ============================================================================
// VALIDATION
// ============================================================================

bool MOBIEngine::ValidateFile(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    // Read PDB header
    uint8_t header[78];
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!file.good()) {
        return false;
    }
    
    return MOBIDetection::IsMOBIType(header, sizeof(header));
}

// ============================================================================
// STATIC DETECTION METHODS
// ============================================================================

bool MOBIEngine::IsMOBIFile(const std::string& filePath) {
    return MOBIDetection::HasMOBIExtension(filePath);
}

bool MOBIEngine::IsMOBIData(const uint8_t* data, size_t size) {
    return MOBIDetection::IsMOBIType(data, size);
}

// ============================================================================
// PDB PARSING
// ============================================================================

bool MOBIEngine::ParsePDBHeader(const uint8_t* data, size_t size) {
    if (size < sizeof(PDBHeader)) {
        lastError = "Data too small for PDB header";
        return false;
    }
    
    // Copy header (need to handle endianness)
    std::memcpy(parsedData.pdbHeader.name, data, 32);
    parsedData.pdbHeader.attributes = ReadBE16(data + 32);
    parsedData.pdbHeader.version = ReadBE16(data + 34);
    parsedData.pdbHeader.creationDate = ReadBE32(data + 36);
    parsedData.pdbHeader.modificationDate = ReadBE32(data + 40);
    parsedData.pdbHeader.lastBackupDate = ReadBE32(data + 44);
    parsedData.pdbHeader.modificationNumber = ReadBE32(data + 48);
    parsedData.pdbHeader.appInfoOffset = ReadBE32(data + 52);
    parsedData.pdbHeader.sortInfoOffset = ReadBE32(data + 56);
    std::memcpy(parsedData.pdbHeader.type, data + 60, 4);
    std::memcpy(parsedData.pdbHeader.creator, data + 64, 4);
    parsedData.pdbHeader.uniqueIdSeed = ReadBE32(data + 68);
    parsedData.pdbHeader.nextRecordListId = ReadBE32(data + 72);
    parsedData.pdbHeader.recordCount = ReadBE16(data + 76);
    
    // Validate type and creator
    std::string type(parsedData.pdbHeader.type, 4);
    std::string creator(parsedData.pdbHeader.creator, 4);
    
    if (type != "BOOK" || creator != "MOBI") {
        // Also accept TEXt/REAd for older PalmDOC
        if (type != "TEXt" || creator != "REAd") {
            lastError = "Not a MOBI file (type: " + type + ", creator: " + creator + ")";
            return false;
        }
    }
    
    return true;
}

bool MOBIEngine::ParseRecordList(const uint8_t* data, size_t size) {
    size_t recordListOffset = 78;
    size_t recordListSize = parsedData.pdbHeader.recordCount * 8;
    
    if (size < recordListOffset + recordListSize) {
        lastError = "Data too small for record list";
        return false;
    }
    
    parsedData.recordEntries.clear();
    parsedData.recordEntries.reserve(parsedData.pdbHeader.recordCount);
    
    const uint8_t* ptr = data + recordListOffset;
    for (int i = 0; i < parsedData.pdbHeader.recordCount; ++i) {
        PDBRecordEntry entry;
        entry.offset = ReadBE32(ptr);
        entry.attributes = ptr[4];
        std::memcpy(entry.uniqueId, ptr + 5, 3);
        
        parsedData.recordEntries.push_back(entry);
        ptr += 8;
    }
    
    return true;
}

bool MOBIEngine::ExtractRecords(const uint8_t* data, size_t size) {
    parsedData.records.clear();
    parsedData.records.reserve(parsedData.recordEntries.size());
    
    for (size_t i = 0; i < parsedData.recordEntries.size(); ++i) {
        uint32_t offset = parsedData.recordEntries[i].offset;
        uint32_t nextOffset;
        
        if (i + 1 < parsedData.recordEntries.size()) {
            nextOffset = parsedData.recordEntries[i + 1].offset;
        } else {
            nextOffset = static_cast<uint32_t>(size);
        }
        
        if (offset >= size || nextOffset > size || nextOffset < offset) {
            lastError = "Invalid record offset";
            return false;
        }
        
        size_t recordSize = nextOffset - offset;
        std::vector<uint8_t> record(data + offset, data + offset + recordSize);
        parsedData.records.push_back(std::move(record));
    }
    
    return true;
}

// ============================================================================
// RECORD 0 PARSING
// ============================================================================

bool MOBIEngine::ParseRecord0(const std::vector<uint8_t>& record0) {
    if (record0.size() < 16) {
        lastError = "Record 0 too small";
        return false;
    }
    
    // Parse PalmDOC header (first 16 bytes)
    if (!ParsePalmDOCHeader(record0.data(), record0.size())) {
        return false;
    }
    
    // Check for MOBI header (starts at offset 16)
    if (record0.size() >= 20) {
        if (std::memcmp(record0.data() + 16, "MOBI", 4) == 0) {
            if (!ParseMOBIHeader(record0.data() + 16, record0.size() - 16)) {
                return false;
            }
            
            // Check for EXTH header
            if (parsedData.hasEXTH) {
                size_t exthOffset = 16 + parsedData.mobiHeader.headerLength;
                if (exthOffset < record0.size()) {
                    ParseEXTHHeader(record0.data() + exthOffset, 
                                    record0.size() - exthOffset);
                }
            }
            
            // Extract full name
            if (parsedData.mobiHeader.fullNameOffset > 0 &&
                parsedData.mobiHeader.fullNameOffset < record0.size()) {
                size_t nameOffset = parsedData.mobiHeader.fullNameOffset;
                size_t nameLength = std::min(
                    static_cast<size_t>(parsedData.mobiHeader.fullNameLength),
                    record0.size() - nameOffset);
                parsedData.fullName.assign(
                    reinterpret_cast<const char*>(record0.data() + nameOffset),
                    nameLength);
            }
        }
    }
    
    return true;
}

bool MOBIEngine::ParsePalmDOCHeader(const uint8_t* data, size_t size) {
    if (size < 16) {
        return false;
    }
    
    parsedData.palmDocHeader.compression = ReadBE16(data);
    parsedData.palmDocHeader.unused = ReadBE16(data + 2);
    parsedData.palmDocHeader.textLength = ReadBE32(data + 4);
    parsedData.palmDocHeader.recordCount = ReadBE16(data + 8);
    parsedData.palmDocHeader.recordSize = ReadBE16(data + 10);
    parsedData.palmDocHeader.currentPosition = ReadBE32(data + 12);
    
    // Set compression type
    parsedData.compression = static_cast<MOBICompression>(
        parsedData.palmDocHeader.compression);
    
    // Check for encryption (encoded in currentPosition for some versions)
    uint16_t encryptionType = ReadBE16(data + 12);
    parsedData.isEncrypted = (encryptionType != 0);
    
    // Determine text record range
    parsedData.firstTextRecord = 1;
    parsedData.lastTextRecord = parsedData.palmDocHeader.recordCount;
    
    return true;
}

bool MOBIEngine::ParseMOBIHeader(const uint8_t* data, size_t size) {
    if (size < 8) {
        return false;
    }
    
    // Read header length first
    uint32_t headerLength = ReadBE32(data + 4);
    if (size < headerLength) {
        // Header truncated, but we can still read what's available
    }
    
    // Parse MOBI header fields
    std::memcpy(parsedData.mobiHeader.identifier, data, 4);
    parsedData.mobiHeader.headerLength = headerLength;
    
    if (size >= 28) {
        parsedData.mobiHeader.mobiType = ReadBE32(data + 8);
        parsedData.mobiHeader.textEncoding = ReadBE32(data + 12);
        parsedData.mobiHeader.uniqueId = ReadBE32(data + 16);
        parsedData.mobiHeader.fileVersion = ReadBE32(data + 20);
        
        parsedData.encoding = static_cast<MOBIEncoding>(
            parsedData.mobiHeader.textEncoding);
    }
    
    if (size >= 92) {
        parsedData.mobiHeader.firstNonBookIndex = ReadBE32(data + 80);
        parsedData.mobiHeader.fullNameOffset = ReadBE32(data + 84);
        parsedData.mobiHeader.fullNameLength = ReadBE32(data + 88);
    }
    
    if (size >= 108) {
        parsedData.mobiHeader.firstImageIndex = ReadBE32(data + 108);
    }
    
    if (size >= 132) {
        parsedData.mobiHeader.exthFlags = ReadBE32(data + 128);
        parsedData.hasEXTH = (parsedData.mobiHeader.exthFlags & 0x40) != 0;
    }
    
    // Check for KF8 format (version 8+)
    if (parsedData.mobiHeader.fileVersion >= 8) {
        parsedData.isKF8 = true;
    }
    
    return true;
}

bool MOBIEngine::ParseEXTHHeader(const uint8_t* data, size_t size) {
    if (size < 12) {
        return false;
    }
    
    // Verify EXTH signature
    if (std::memcmp(data, "EXTH", 4) != 0) {
        return false;
    }
    
    uint32_t headerLength = ReadBE32(data + 4);
    uint32_t recordCount = ReadBE32(data + 8);
    
    if (size < headerLength) {
        // Truncated, parse what we can
    }
    
    // Parse EXTH records
    const uint8_t* ptr = data + 12;
    const uint8_t* end = data + std::min(size, static_cast<size_t>(headerLength));
    
    for (uint32_t i = 0; i < recordCount && ptr + 8 <= end; ++i) {
        uint32_t recordType = ReadBE32(ptr);
        uint32_t recordLength = ReadBE32(ptr + 4);
        
        if (recordLength < 8 || ptr + recordLength > end) {
            break;
        }
        
        // Extract record data as string
        size_t dataLength = recordLength - 8;
        std::string recordData(reinterpret_cast<const char*>(ptr + 8), dataLength);
        
        parsedData.exthRecords[recordType].push_back(recordData);
        
        // Handle special records
        if (recordType == EXTHRecordType::COVER_OFFSET && dataLength >= 4) {
            parsedData.coverImageIndex = 
                static_cast<int>(ReadBE32(ptr + 8)) + 
                static_cast<int>(parsedData.mobiHeader.firstImageIndex);
        }
        if (recordType == EXTHRecordType::THUMB_OFFSET && dataLength >= 4) {
            parsedData.thumbnailImageIndex = 
                static_cast<int>(ReadBE32(ptr + 8)) + 
                static_cast<int>(parsedData.mobiHeader.firstImageIndex);
        }
        
        ptr += recordLength;
    }
    
    return true;
}

// ============================================================================
// CONTENT EXTRACTION
// ============================================================================

bool MOBIEngine::ExtractTextContent() {
    std::vector<uint8_t> decompressedText;
    
    int firstRecord = parsedData.firstTextRecord;
    int lastRecord = std::min(
        parsedData.lastTextRecord,
        static_cast<int>(parsedData.records.size()) - 1);
    
    // Reserve estimated space
    decompressedText.reserve(parsedData.palmDocHeader.textLength);
    
    for (int i = firstRecord; i <= lastRecord; ++i) {
        const auto& record = parsedData.records[i];
        
        std::vector<uint8_t> decompressed;
        
        switch (parsedData.compression) {
            case MOBICompression::None:
                decompressed = record;
                break;
                
            case MOBICompression::PalmDOC:
                decompressed = DecompressPalmDOC(record.data(), record.size());
                break;
                
            case MOBICompression::HuffCDIC:
                decompressed = DecompressHuffCDIC(record.data(), record.size());
                break;
                
            default:
                lastError = "Unknown compression type";
                return false;
        }
        
        decompressedText.insert(decompressedText.end(),
                                decompressed.begin(), decompressed.end());
    }
    
    // Convert to string with proper encoding
    parsedData.textContent = ConvertEncoding(decompressedText);
    
    return true;
}

bool MOBIEngine::ExtractImages() {
    if (parsedData.mobiHeader.firstImageIndex == 0) {
        return true; // No images
    }
    
    int firstImage = static_cast<int>(parsedData.mobiHeader.firstImageIndex);
    int lastRecord = static_cast<int>(parsedData.records.size()) - 1;
    
    // Skip special records at end (FLIS, FCIS, EOF)
    int endOffset = 0;
    for (int i = lastRecord; i >= firstImage && endOffset < 3; --i) {
        const auto& record = parsedData.records[i];
        if (record.size() >= 4) {
            std::string sig(reinterpret_cast<const char*>(record.data()), 4);
            if (sig == "FLIS" || sig == "FCIS" || sig == "BOUN" || 
                sig == "FDST" || sig == "DATP" || sig == "APTS") {
                endOffset++;
            } else {
                break;
            }
        }
    }
    
    for (int i = firstImage; i <= lastRecord - endOffset; ++i) {
        const auto& record = parsedData.records[i];
        
        if (record.size() < 4) continue;
        
        MOBIImageRecord imageRecord;
        imageRecord.recordIndex = i;
        imageRecord.data = record;
        
        // Detect image type from magic bytes
        if (record[0] == 0xFF && record[1] == 0xD8) {
            imageRecord.mimeType = "image/jpeg";
        } else if (record[0] == 0x89 && record[1] == 'P' && 
                   record[2] == 'N' && record[3] == 'G') {
            imageRecord.mimeType = "image/png";
        } else if (record[0] == 'G' && record[1] == 'I' && record[2] == 'F') {
            imageRecord.mimeType = "image/gif";
        } else if (record[0] == 'B' && record[1] == 'M') {
            imageRecord.mimeType = "image/bmp";
        } else {
            // Unknown type, skip
            continue;
        }
        
        // Mark cover and thumbnail
        if (i == parsedData.coverImageIndex) {
            imageRecord.isCover = true;
        }
        if (i == parsedData.thumbnailImageIndex) {
            imageRecord.isThumbnail = true;
        }
        
        parsedData.images.push_back(std::move(imageRecord));
    }
    
    return true;
}

// ============================================================================
// PALMDOC DECOMPRESSION
// ============================================================================

std::vector<uint8_t> MOBIEngine::DecompressPalmDOC(const uint8_t* data, size_t size) {
    return PalmDOCDecompressor::Decompress(data, size);
}

std::vector<uint8_t> PalmDOCDecompressor::Decompress(const uint8_t* data, size_t size,
                                                       int trailingBytes) {
    std::vector<uint8_t> output;
    output.reserve(size * 2); // Estimate
    
    size_t effectiveSize = size - trailingBytes;
    size_t i = 0;
    
    while (i < effectiveSize) {
        uint8_t c = data[i++];
        
        if (c == 0x00) {
            // Literal null byte
            output.push_back(0x00);
        } else if (c >= 0x01 && c <= 0x08) {
            // Copy next 1-8 bytes literally
            int count = c;
            for (int j = 0; j < count && i < effectiveSize; ++j) {
                output.push_back(data[i++]);
            }
        } else if (c >= 0x09 && c <= 0x7F) {
            // Literal byte
            output.push_back(c);
        } else if (c >= 0x80 && c <= 0xBF) {
            // Length-distance pair (2 bytes)
            if (i >= effectiveSize) break;
            
            uint8_t c2 = data[i++];
            
            // Distance is in bits 2-12 (11 bits)
            int distance = ((c & 0x3F) << 5) | (c2 >> 3);
            // Length is in bits 13-15 (3 bits) + 3
            int length = (c2 & 0x07) + 3;
            
            if (distance > 0 && distance <= static_cast<int>(output.size())) {
                size_t srcPos = output.size() - distance;
                for (int j = 0; j < length; ++j) {
                    output.push_back(output[srcPos + j]);
                }
            }
        } else { // c >= 0xC0
            // Space + character
            output.push_back(' ');
            output.push_back(c ^ 0x80);
        }
    }
    
    return output;
}

std::vector<uint8_t> PalmDOCDecompressor::Compress(const uint8_t* data, size_t size) {
    // Compression implementation (simplified)
    std::vector<uint8_t> output;
    output.reserve(size);
    
    // For now, just output uncompressed with literal markers
    size_t i = 0;
    while (i < size) {
        uint8_t c = data[i];
        
        if (c == 0x00 || (c >= 0x09 && c <= 0x7F)) {
            // Can be output directly
            output.push_back(c);
            ++i;
        } else if (c >= 0x01 && c <= 0x08) {
            // Need to escape with type A command
            output.push_back(0x01);
            output.push_back(c);
            ++i;
        } else {
            // Need to escape
            output.push_back(0x01);
            output.push_back(c);
            ++i;
        }
    }
    
    return output;
}

// ============================================================================
// HUFF/CDIC DECOMPRESSION
// ============================================================================

std::vector<uint8_t> MOBIEngine::DecompressHuffCDIC(const uint8_t* data, size_t size) {
    // HUFF/CDIC decompression requires Huffman tables from HUFF records
    // and dictionary data from CDIC records
    // This is a complex algorithm - for now, return empty
    lastError = "HUFF/CDIC compression not yet fully implemented";
    return std::vector<uint8_t>();
}

bool HuffCDICDecompressor::Initialize(const std::vector<std::vector<uint8_t>>& huffRecords,
                                       const std::vector<std::vector<uint8_t>>& cdicRecords) {
    if (huffRecords.empty() || cdicRecords.empty()) {
        return false;
    }
    
    // Parse HUFF record
    const auto& huff = huffRecords[0];
    if (huff.size() < 24 || std::memcmp(huff.data(), "HUFF", 4) != 0) {
        return false;
    }
    
    // Extract dictionary tables
    uint32_t offset1 = MOBIEngine::ReadBE32(huff.data() + 8);
    uint32_t offset2 = MOBIEngine::ReadBE32(huff.data() + 12);
    
    // Build dict1 (256 entries)
    dict1.resize(256);
    for (int i = 0; i < 256 && offset1 + i * 4 + 4 <= huff.size(); ++i) {
        dict1[i] = MOBIEngine::ReadBE32(huff.data() + offset1 + i * 4);
    }
    
    // Build dict2 (64 entries)
    dict2.resize(64);
    for (int i = 0; i < 64 && offset2 + i * 4 + 4 <= huff.size(); ++i) {
        dict2[i] = MOBIEngine::ReadBE32(huff.data() + offset2 + i * 4);
    }
    
    // Store CDIC data
    cdicData = cdicRecords;
    cdicCount = static_cast<int>(cdicRecords.size());
    
    initialized = true;
    return true;
}

std::vector<uint8_t> HuffCDICDecompressor::Decompress(const uint8_t* data, size_t size) {
    if (!initialized) {
        return std::vector<uint8_t>();
    }
    
    // Huffman decompression algorithm
    std::vector<uint8_t> output;
    // ... Complex implementation would go here
    
    return output;
}

// ============================================================================
// ENCODING CONVERSION
// ============================================================================

std::string MOBIEngine::ConvertEncoding(const std::vector<uint8_t>& data) const {
    if (data.empty()) {
        return std::string();
    }
    
    if (parsedData.encoding == MOBIEncoding::UTF8) {
        // Already UTF-8
        return std::string(data.begin(), data.end());
    }
    
    // Convert from CP1252 to UTF-8
    std::string result;
    result.reserve(data.size() * 2);
    
    for (uint8_t c : data) {
        if (c < 0x80) {
            result.push_back(static_cast<char>(c));
        } else {
            // CP1252 to Unicode mapping for characters 0x80-0x9F
            static const uint16_t cp1252_map[] = {
                0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
                0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
            };
            
            uint16_t unicode;
            if (c >= 0x80 && c <= 0x9F) {
                unicode = cp1252_map[c - 0x80];
            } else {
                unicode = c; // Latin-1 Supplement (0xA0-0xFF)
            }
            
            // Encode as UTF-8
            if (unicode < 0x80) {
                result.push_back(static_cast<char>(unicode));
            } else if (unicode < 0x800) {
                result.push_back(static_cast<char>(0xC0 | (unicode >> 6)));
                result.push_back(static_cast<char>(0x80 | (unicode & 0x3F)));
            } else {
                result.push_back(static_cast<char>(0xE0 | (unicode >> 12)));
                result.push_back(static_cast<char>(0x80 | ((unicode >> 6) & 0x3F)));
                result.push_back(static_cast<char>(0x80 | (unicode & 0x3F)));
            }
        }
    }
    
    return result;
}

// ============================================================================
// METADATA BUILDING
// ============================================================================

void MOBIEngine::BuildMetadata() {
    // Title
    if (!parsedData.fullName.empty()) {
        metadata.title = parsedData.fullName;
    } else {
        // Use database name from PDB header
        metadata.title = std::string(parsedData.pdbHeader.name, 
                                     strnlen(parsedData.pdbHeader.name, 32));
    }
    
    // EXTH title override
    std::string exthTitle = GetEXTHString(EXTHRecordType::UPDATED_TITLE);
    if (!exthTitle.empty()) {
        metadata.title = exthTitle;
    }
    
    // Author
    metadata.author = GetEXTHString(EXTHRecordType::AUTHOR);
    metadata.creators = GetEXTHStrings(EXTHRecordType::AUTHOR);
    
    // Publisher
    metadata.publisher = GetEXTHString(EXTHRecordType::PUBLISHER);
    
    // Description
    metadata.description = GetEXTHString(EXTHRecordType::DESCRIPTION);
    
    // ISBN
    metadata.isbn = GetEXTHString(EXTHRecordType::ISBN);
    
    // Language
    metadata.language = GetEXTHString(EXTHRecordType::LANGUAGE);
    
    // Subjects
    metadata.subjects = GetEXTHStrings(EXTHRecordType::SUBJECT);
    
    // Rights
    metadata.rights = GetEXTHString(EXTHRecordType::RIGHTS);
    
    // Publication date
    metadata.date = GetEXTHString(EXTHRecordType::PUBLISHING_DATE);
    
    // ASIN (Amazon ID)
    std::string asin = GetEXTHString(EXTHRecordType::ASIN);
    if (!asin.empty()) {
        metadata.identifier = asin;
    }
}

std::string MOBIEngine::GetEXTHString(uint32_t recordType) const {
    auto it = parsedData.exthRecords.find(recordType);
    if (it != parsedData.exthRecords.end() && !it->second.empty()) {
        return it->second[0];
    }
    return std::string();
}

std::vector<std::string> MOBIEngine::GetEXTHStrings(uint32_t recordType) const {
    auto it = parsedData.exthRecords.find(recordType);
    if (it != parsedData.exthRecords.end()) {
        return it->second;
    }
    return std::vector<std::string>();
}

uint32_t MOBIEngine::GetEXTHUInt32(uint32_t recordType) const {
    auto it = parsedData.exthRecords.find(recordType);
    if (it != parsedData.exthRecords.end() && !it->second.empty() && 
        it->second[0].size() >= 4) {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(it->second[0].data());
        return ReadBE32(data);
    }
    return 0;
}

// ============================================================================
// TABLE OF CONTENTS
// ============================================================================

void MOBIEngine::BuildTableOfContents() {
    tableOfContents.clear();
    
    // Extract TOC from HTML content
    // Look for <h1>, <h2>, <h3> tags or guide references
    std::regex headingRegex(R"(<h([1-6])[^>]*>([^<]+)</h\1>)", std::regex::icase);
    
    std::string::const_iterator searchStart = parsedData.textContent.cbegin();
    std::smatch match;
    int entryIndex = 0;
    
    while (std::regex_search(searchStart, parsedData.textContent.cend(), match, headingRegex)) {
        EBookTOCEntry entry;
        entry.title = match[2].str();
        entry.level = std::stoi(match[1].str()) - 1;
        entry.pageNumber = entryIndex + 1; // Simplified
        
        // Remove HTML entities
        std::regex entityRegex(R"(&[a-z]+;)");
        entry.title = std::regex_replace(entry.title, entityRegex, "");
        
        // Trim whitespace
        entry.title.erase(0, entry.title.find_first_not_of(" \t\n\r"));
        entry.title.erase(entry.title.find_last_not_of(" \t\n\r") + 1);
        
        if (!entry.title.empty()) {
            tableOfContents.push_back(entry);
        }
        
        searchStart = match.suffix().first;
        ++entryIndex;
    }
}

// ============================================================================
// PAGE CALCULATION
// ============================================================================

void MOBIEngine::CalculatePages() {
    // Estimate pages based on content length
    // Typical page: ~2000 characters
    const int CHARS_PER_PAGE = 2000;
    
    size_t contentLength = parsedData.textContent.length();
    pageCount = std::max(1, static_cast<int>(contentLength / CHARS_PER_PAGE));
    
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

EBookMetadata MOBIEngine::GetMetadata() const {
    return metadata;
}

int MOBIEngine::GetPageCount() const {
    return pageCount;
}

EBookPageInfo MOBIEngine::GetPageInfo(int pageNumber) const {
    if (pageNumber < 1 || pageNumber > static_cast<int>(pageInfos.size())) {
        return EBookPageInfo();
    }
    return pageInfos[pageNumber - 1];
}

std::vector<EBookTOCEntry> MOBIEngine::GetTableOfContents() const {
    return tableOfContents;
}

std::string MOBIEngine::GetPageContent(int pageNumber) const {
    if (pageNumber < 1 || pageNumber > pageCount) {
        return std::string();
    }
    
    const auto& info = pageInfos[pageNumber - 1];
    if (info.contentOffset >= static_cast<int>(parsedData.textContent.length())) {
        return std::string();
    }
    
    return parsedData.textContent.substr(info.contentOffset, info.contentLength);
}

std::vector<uint8_t> MOBIEngine::RenderPageToImage(int pageNumber,
                                                    int width, int height,
                                                    float dpi) const {
    // Rendering would use HTMLConverter
    // For now, return empty
    return std::vector<uint8_t>();
}

std::string MOBIEngine::GetHTMLContent() const {
    return parsedData.textContent;
}

const MOBIParsedData& MOBIEngine::GetParsedData() const {
    return parsedData;
}

// ============================================================================
// IMAGE ACCESS
// ============================================================================

std::vector<uint8_t> MOBIEngine::GetCoverImage() const {
    for (const auto& image : parsedData.images) {
        if (image.isCover) {
            return image.data;
        }
    }
    
    // No explicit cover, try first image
    if (!parsedData.images.empty()) {
        return parsedData.images[0].data;
    }
    
    return std::vector<uint8_t>();
}

const MOBIImageRecord* MOBIEngine::GetImage(int index) const {
    if (index < 0 || index >= static_cast<int>(parsedData.images.size())) {
        return nullptr;
    }
    return &parsedData.images[index];
}

int MOBIEngine::GetImageCount() const {
    return static_cast<int>(parsedData.images.size());
}

bool MOBIEngine::IsEncrypted() const {
    return parsedData.isEncrypted;
}

bool MOBIEngine::IsKF8Format() const {
    return parsedData.isKF8;
}

// ============================================================================
// SEARCH
// ============================================================================

std::vector<EBookSearchResult> MOBIEngine::Search(const std::string& query) const {
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

std::string MOBIEngine::GetLastError() const {
    return lastError;
}

void MOBIEngine::SetProgressCallback(std::function<void(float, const std::string&)> callback) {
    progressCallback = callback;
}

void MOBIEngine::ReportProgress(float progress, const std::string& status) {
    if (progressCallback) {
        progressCallback(progress, status);
    }
}

// ============================================================================
// DETECTION UTILITIES
// ============================================================================

namespace MOBIDetection {

bool HasMOBIExtension(const std::string& filePath) {
    size_t dotPos = filePath.find_last_of('.');
    if (dotPos == std::string::npos) {
        return false;
    }
    
    std::string ext = filePath.substr(dotPos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    return ext == "mobi" || ext == "prc" || ext == "azw";
}

bool IsPDBFormat(const uint8_t* data, size_t size) {
    if (size < 78) {
        return false;
    }
    
    // Check for reasonable values
    uint16_t recordCount = MOBIEngine::ReadBE16(data + 76);
    if (recordCount == 0 || recordCount > 10000) {
        return false;
    }
    
    return true;
}

bool IsMOBIType(const uint8_t* data, size_t size) {
    if (size < 68) {
        return false;
    }
    
    // Check type and creator fields at offset 60 and 64
    std::string type(reinterpret_cast<const char*>(data + 60), 4);
    std::string creator(reinterpret_cast<const char*>(data + 64), 4);
    
    // MOBI books have type "BOOK" and creator "MOBI"
    if (type == "BOOK" && creator == "MOBI") {
        return true;
    }
    
    // Older PalmDOC has type "TEXt" and creator "REAd"
    if (type == "TEXt" && creator == "REAd") {
        return true;
    }
    
    return false;
}

std::string GetCreatorCode(const uint8_t* data, size_t size) {
    if (size < 68) {
        return "";
    }
    return std::string(reinterpret_cast<const char*>(data + 64), 4);
}

std::string GetTypeCode(const uint8_t* data, size_t size) {
    if (size < 64) {
        return "";
    }
    return std::string(reinterpret_cast<const char*>(data + 60), 4);
}

} // namespace MOBIDetection

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

std::unique_ptr<IEBookEngine> CreateMOBIEngine() {
    return std::make_unique<MOBIEngine>();
}

void RegisterMOBIPlugin() {
    // Register with EBookEngineFactory
    // EBookEngineFactory::RegisterEngine("mobi", CreateMOBIEngine);
    // EBookEngineFactory::RegisterEngine("prc", CreateMOBIEngine);
    // EBookEngineFactory::RegisterEngine("azw", CreateMOBIEngine);
}

void UnregisterMOBIPlugin() {
    // Unregister from EBookEngineFactory
    // EBookEngineFactory::UnregisterEngine("mobi");
    // EBookEngineFactory::UnregisterEngine("prc");
    // EBookEngineFactory::UnregisterEngine("azw");
}

} // namespace UltraCanvas
