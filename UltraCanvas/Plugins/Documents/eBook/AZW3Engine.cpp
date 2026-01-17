// Plugins/Documents/eBook/AZW3Engine.cpp
// Amazon Kindle Format 8 (KF8/AZW3) eBook Engine Implementation
// Version: 1.0.0
// Last Modified: 2025-01-10
// Author: UltraCanvas Framework

#include "AZW3Engine.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <cstring>

namespace UltraCanvas {

// ============================================================================
// CONSTRUCTOR & DESTRUCTOR
// ============================================================================

AZW3Engine::AZW3Engine()
    : MOBIEngine()
    , preferKF8(true) {
}

AZW3Engine::~AZW3Engine() {
    // Cleanup handled by base class
}

// ============================================================================
// FORMAT IDENTIFICATION
// ============================================================================

EBookFormat AZW3Engine::GetFormat() const {
    return EBookFormat::AZW3;
}

std::string AZW3Engine::GetFormatName() const {
    if (kf8Data.hasKF8Content) {
        return "Kindle Format 8 (KF8/AZW3)";
    }
    return "Amazon Kindle (AZW3)";
}

std::vector<std::string> AZW3Engine::GetSupportedExtensions() const {
    return { "azw3", "kf8" };
}

// ============================================================================
// DOCUMENT LOADING
// ============================================================================

bool AZW3Engine::LoadFile(const std::string& filePath, const std::string& password) {
    // First, load as MOBI (handles PDB structure)
    if (!MOBIEngine::LoadFile(filePath, password)) {
        return false;
    }
    
    // Then parse KF8 content
    if (!ParseKF8Content()) {
        // KF8 parsing failed, but MOBI content is still available
        // This is acceptable - file may be pure MOBI with .azw3 extension
    }
    
    return true;
}

bool AZW3Engine::LoadFromMemory(const std::vector<uint8_t>& data,
                                 const std::string& password) {
    // First, load as MOBI
    if (!MOBIEngine::LoadFromMemory(data, password)) {
        return false;
    }
    
    // Then parse KF8 content
    ParseKF8Content();
    
    return true;
}

// ============================================================================
// VALIDATION
// ============================================================================

bool AZW3Engine::ValidateFile(const std::string& filePath) {
    // First check if it's a valid MOBI
    if (!MOBIEngine::ValidateFile(filePath)) {
        return false;
    }
    
    // Check extension
    return AZW3Detection::HasAZW3Extension(filePath);
}

// ============================================================================
// STATIC DETECTION METHODS
// ============================================================================

bool AZW3Engine::IsAZW3File(const std::string& filePath) {
    return AZW3Detection::HasAZW3Extension(filePath);
}

bool AZW3Engine::IsAZW3Data(const uint8_t* data, size_t size) {
    // First check if it's valid MOBI
    if (!MOBIEngine::IsMOBIData(data, size)) {
        return false;
    }
    
    // Then check for KF8 content
    return AZW3Detection::HasKF8Content(data, size);
}

// ============================================================================
// KF8 CONTENT PARSING
// ============================================================================

bool AZW3Engine::ParseKF8Content() {
    ReportProgress(0.75f, "Parsing KF8 content...");
    
    // Find KF8 boundary
    if (!FindKF8Boundary()) {
        // No KF8 content, MOBI only
        kf8Data.hasKF8Content = false;
        return false;
    }
    
    kf8Data.hasKF8Content = true;
    
    // Parse FDST (Fragment Data Start Table)
    int fdstIndex = FindRecordByType(KF8RecordType::FDST, kf8Data.kf8BoundaryOffset);
    if (fdstIndex >= 0 && fdstIndex < static_cast<int>(GetParsedData().records.size())) {
        ParseFDST(GetParsedData().records[fdstIndex]);
    }
    
    // Parse fragments
    ParseFragments();
    
    // Parse resources (images, CSS)
    ParseResources();
    
    // Parse embedded fonts
    ParseFonts();
    
    // Parse NCX navigation
    ParseNCX();
    
    // Parse guide items
    ParseGuide();
    
    // Reconstruct HTML from fragments
    ReconstructHTML();
    
    // Build navigation
    BuildKF8Navigation();
    
    // Calculate pages based on KF8 content
    CalculateKF8Pages();
    
    ReportProgress(0.85f, "KF8 parsing complete");
    
    return true;
}

bool AZW3Engine::FindKF8Boundary() {
    // Check for KF8 boundary in EXTH records (record type 121)
    const auto& parsedData = GetParsedData();
    
    auto it = parsedData.exthRecords.find(EXTHRecordType::KF8_BOUNDARY);
    if (it != parsedData.exthRecords.end() && !it->second.empty()) {
        const std::string& data = it->second[0];
        if (data.size() >= 4) {
            kf8Data.kf8BoundaryOffset = ReadBE32(
                reinterpret_cast<const uint8_t*>(data.data()));
            return kf8Data.kf8BoundaryOffset > 0;
        }
    }
    
    // Also check MOBI version (8+ indicates KF8)
    if (AZW3Detection::IsKF8Version(parsedData.mobiHeader.fileVersion)) {
        // Version 8+ but no explicit boundary - search for KF8 records
        kf8Data.kf8BoundaryOffset = parsedData.firstTextRecord + 
                                     parsedData.palmDocHeader.recordCount;
        return true;
    }
    
    return false;
}

bool AZW3Engine::ParseFDST(const std::vector<uint8_t>& record) {
    if (record.size() < sizeof(FDSTHeader)) {
        return false;
    }
    
    // Verify FDST signature
    if (std::memcmp(record.data(), "FDST", 4) != 0) {
        return false;
    }
    
    uint32_t headerLength = ReadBE32(record.data() + 4);
    uint32_t fragmentCount = ReadBE32(record.data() + 8);
    
    // Parse fragment offsets
    kf8Data.flowRanges.clear();
    kf8Data.flowCount = static_cast<int>(fragmentCount);
    
    const uint8_t* ptr = record.data() + 12;
    uint32_t prevOffset = 0;
    
    for (uint32_t i = 0; i < fragmentCount && ptr + 4 <= record.data() + record.size(); ++i) {
        uint32_t offset = ReadBE32(ptr);
        
        if (i > 0) {
            kf8Data.flowRanges.push_back({prevOffset, offset - prevOffset});
        }
        
        prevOffset = offset;
        ptr += 4;
    }
    
    // Last fragment
    if (fragmentCount > 0) {
        // Estimate last fragment length
        kf8Data.flowRanges.push_back({prevOffset, 4096}); // Approximate
    }
    
    return true;
}

bool AZW3Engine::ParseSkeleton(const std::vector<uint8_t>& record) {
    // Parse skeleton structure
    if (record.size() < 8 || std::memcmp(record.data(), "SKEL", 4) != 0) {
        return false;
    }
    
    KF8Skeleton skeleton;
    skeleton.index = static_cast<uint32_t>(kf8Data.skeletons.size());
    
    const uint8_t* ptr = record.data() + 4;
    const uint8_t* end = record.data() + record.size();
    
    // Parse skeleton entries
    while (ptr + 8 <= end) {
        uint32_t fragIndex = ReadBE32(ptr);
        uint32_t nameLen = ReadBE32(ptr + 4);
        ptr += 8;
        
        if (ptr + nameLen <= end) {
            skeleton.name.assign(reinterpret_cast<const char*>(ptr), nameLen);
            ptr += nameLen;
        }
        
        skeleton.fragmentIds.push_back(fragIndex);
        skeleton.fragmentCount++;
    }
    
    kf8Data.skeletons.push_back(std::move(skeleton));
    return true;
}

bool AZW3Engine::ParseFragments() {
    const auto& parsedData = GetParsedData();
    
    // Fragments are stored after the KF8 boundary
    // Each fragment contains HTML content
    
    kf8Data.fragments.clear();
    
    for (size_t i = kf8Data.kf8BoundaryOffset; i < parsedData.records.size(); ++i) {
        const auto& record = parsedData.records[i];
        
        // Skip non-fragment records
        if (record.size() < 4) continue;
        
        // Check for end markers
        std::string sig(reinterpret_cast<const char*>(record.data()), 4);
        if (sig == "FLIS" || sig == "FCIS" || sig == "FDST" || 
            sig == "SKEL" || sig == "INDX" || sig == "DATP") {
            continue;
        }
        
        // This might be a fragment
        KF8Fragment fragment;
        fragment.index = static_cast<uint32_t>(kf8Data.fragments.size());
        fragment.filePosition = static_cast<uint32_t>(i);
        fragment.length = static_cast<uint32_t>(record.size());
        
        // Decompress content
        fragment.content = DecompressFragment(record.data(), record.size());
        
        if (!fragment.content.empty()) {
            kf8Data.fragments.push_back(std::move(fragment));
        }
    }
    
    return !kf8Data.fragments.empty();
}

bool AZW3Engine::ParseResources() {
    const auto& parsedData = GetParsedData();
    
    // Resources include images, CSS, and other assets
    // They're typically after the MOBI image section
    
    kf8Data.resources.clear();
    
    // Look for RESC records
    for (size_t i = 0; i < parsedData.records.size(); ++i) {
        const auto& record = parsedData.records[i];
        
        if (record.size() < 4) continue;
        
        if (std::memcmp(record.data(), "RESC", 4) == 0) {
            // Parse resource record
            KF8Resource resource;
            resource.index = static_cast<uint32_t>(kf8Data.resources.size());
            
            // RESC format: RESC + length + type + data
            if (record.size() > 12) {
                uint32_t resType = ReadBE32(record.data() + 8);
                
                // Determine resource type
                switch (resType) {
                    case 1:
                        resource.type = "css";
                        resource.mediaType = "text/css";
                        break;
                    case 2:
                        resource.type = "image";
                        break;
                    default:
                        resource.type = "unknown";
                        break;
                }
                
                resource.data.assign(record.begin() + 12, record.end());
            }
            
            kf8Data.resources.push_back(std::move(resource));
        }
    }
    
    return true;
}

bool AZW3Engine::ParseFonts() {
    const auto& parsedData = GetParsedData();
    
    kf8Data.fonts.clear();
    
    // Look for font records (typically after KF8 boundary)
    for (size_t i = kf8Data.kf8BoundaryOffset; i < parsedData.records.size(); ++i) {
        const auto& record = parsedData.records[i];
        
        if (record.size() < 4) continue;
        
        // Check for font magic bytes
        // TTF/OTF: starts with various signatures
        // WOFF: starts with "wOFF"
        
        bool isFont = false;
        std::string format;
        
        if (record.size() >= 4) {
            // Check TTF signature (0x00010000 or "OTTO" for OTF)
            if ((record[0] == 0x00 && record[1] == 0x01 && 
                 record[2] == 0x00 && record[3] == 0x00) ||
                (record[0] == 'O' && record[1] == 'T' && 
                 record[2] == 'T' && record[3] == 'O')) {
                isFont = true;
                format = "ttf";
            }
            // Check WOFF signature
            else if (record[0] == 'w' && record[1] == 'O' && 
                     record[2] == 'F' && record[3] == 'F') {
                isFont = true;
                format = "woff";
            }
        }
        
        if (isFont) {
            KF8Font font;
            font.index = static_cast<uint32_t>(kf8Data.fonts.size());
            font.data = record;
            font.fontFamily = KF8FontHandler::GetFontFamily(record.data(), record.size());
            font.isObfuscated = KF8FontHandler::IsObfuscated(record.data(), record.size());
            
            // Deobfuscate if necessary
            if (font.isObfuscated) {
                DeobfuscateFont(font);
            }
            
            kf8Data.fonts.push_back(std::move(font));
        }
    }
    
    return true;
}

bool AZW3Engine::ParseNCX() {
    // NCX (Navigation Control for XML) contains TOC
    // Look for NCXC record
    
    const auto& parsedData = GetParsedData();
    
    for (size_t i = 0; i < parsedData.records.size(); ++i) {
        const auto& record = parsedData.records[i];
        
        if (record.size() < 4) continue;
        
        if (std::memcmp(record.data(), "NCXC", 4) == 0 ||
            std::memcmp(record.data(), "NCX ", 4) == 0) {
            // Parse NCX content
            std::string ncxContent(record.begin() + 4, record.end());
            
            // Extract nav points
            std::regex navPointRegex(
                R"(<navPoint[^>]*>.*?<text>([^<]+)</text>.*?<content\s+src="([^"]+)")",
                std::regex::icase);
            
            std::string::const_iterator searchStart = ncxContent.cbegin();
            std::smatch match;
            int order = 0;
            
            while (std::regex_search(searchStart, ncxContent.cend(), match, navPointRegex)) {
                EBookTOCEntry entry;
                entry.title = match[1].str();
                entry.href = match[2].str();
                entry.pageNumber = ++order;
                entry.level = 0;
                
                kf8Data.ncx.push_back(entry);
                searchStart = match.suffix().first;
            }
            
            break;
        }
    }
    
    return !kf8Data.ncx.empty();
}

bool AZW3Engine::ParseGuide() {
    // Guide contains reference points (cover, toc, text start)
    // Usually in EXTH records or GUID record
    
    const auto& parsedData = GetParsedData();
    
    kf8Data.guide.clear();
    
    // Look for GUID record
    for (size_t i = 0; i < parsedData.records.size(); ++i) {
        const auto& record = parsedData.records[i];
        
        if (record.size() < 4) continue;
        
        if (std::memcmp(record.data(), "GUID", 4) == 0) {
            // Parse guide entries
            const uint8_t* ptr = record.data() + 4;
            const uint8_t* end = record.data() + record.size();
            
            while (ptr + 8 <= end) {
                KF8GuideItem item;
                
                uint32_t typeLen = ReadBE32(ptr);
                ptr += 4;
                
                if (ptr + typeLen > end) break;
                item.type.assign(reinterpret_cast<const char*>(ptr), typeLen);
                ptr += typeLen;
                
                uint32_t titleLen = ReadBE32(ptr);
                ptr += 4;
                
                if (ptr + titleLen > end) break;
                item.title.assign(reinterpret_cast<const char*>(ptr), titleLen);
                ptr += titleLen;
                
                if (ptr + 4 <= end) {
                    item.filePosition = ReadBE32(ptr);
                    ptr += 4;
                }
                
                kf8Data.guide.push_back(std::move(item));
            }
            
            break;
        }
    }
    
    return true;
}

bool AZW3Engine::ParseINDX(const std::vector<uint8_t>& record) {
    if (record.size() < sizeof(INDXHeader)) {
        return false;
    }
    
    if (std::memcmp(record.data(), "INDX", 4) != 0) {
        return false;
    }
    
    // Parse index header
    INDXHeader header;
    std::memcpy(&header, record.data(), sizeof(header));
    
    // Index parsing is complex and format-specific
    // For now, extract basic structure
    
    return true;
}

bool AZW3Engine::ParseTAGX(const uint8_t* data, size_t size) {
    // TAGX contains tag definitions for index entries
    if (size < 4 || std::memcmp(data, "TAGX", 4) != 0) {
        return false;
    }
    
    // Parse tag definitions
    // Each tag: 1 byte tag, 1 byte value count, 1 byte bitmask, 1 byte control
    
    return true;
}

bool AZW3Engine::ParseIDXT(const uint8_t* data, size_t size) {
    // IDXT contains index entry offsets
    if (size < 4 || std::memcmp(data, "IDXT", 4) != 0) {
        return false;
    }
    
    // Parse offset table
    
    return true;
}

// ============================================================================
// HTML RECONSTRUCTION
// ============================================================================

bool AZW3Engine::ReconstructHTML() {
    std::ostringstream html;
    
    // Build HTML document from fragments
    html << "<!DOCTYPE html>\n";
    html << "<html>\n<head>\n";
    html << "<meta charset=\"UTF-8\">\n";
    html << "<title>" << GetMetadata().title << "</title>\n";
    
    // Include CSS
    if (!kf8Data.cssContent.empty()) {
        html << "<style>\n" << kf8Data.cssContent << "\n</style>\n";
    }
    
    html << "</head>\n<body>\n";
    
    // Add fragment content
    for (const auto& fragment : kf8Data.fragments) {
        html << fragment.content << "\n";
    }
    
    html << "</body>\n</html>";
    
    kf8Data.htmlContent = html.str();
    
    return true;
}

std::string AZW3Engine::DecompressFragment(const uint8_t* data, size_t size) {
    // Fragments may be compressed with PalmDOC or stored raw
    
    // Check if it looks like HTML
    if (size > 0 && (data[0] == '<' || data[0] == ' ' || 
                     (data[0] >= 'a' && data[0] <= 'z') ||
                     (data[0] >= 'A' && data[0] <= 'Z'))) {
        // Likely raw HTML
        return std::string(reinterpret_cast<const char*>(data), size);
    }
    
    // Try PalmDOC decompression
    auto decompressed = PalmDOCDecompressor::Decompress(data, size);
    if (!decompressed.empty()) {
        return std::string(decompressed.begin(), decompressed.end());
    }
    
    // Return as-is
    return std::string(reinterpret_cast<const char*>(data), size);
}

// ============================================================================
// FONT HANDLING
// ============================================================================

bool AZW3Engine::ExtractFont(int recordIndex, KF8Font& font) {
    const auto& parsedData = GetParsedData();
    
    if (recordIndex < 0 || recordIndex >= static_cast<int>(parsedData.records.size())) {
        return false;
    }
    
    const auto& record = parsedData.records[recordIndex];
    font.data = record;
    font.fontFamily = KF8FontHandler::GetFontFamily(record.data(), record.size());
    
    return true;
}

bool AZW3Engine::DeobfuscateFont(KF8Font& font) {
    if (!font.isObfuscated || font.data.empty()) {
        return true;
    }
    
    // Use book identifier for deobfuscation
    std::string identifier = GetMetadata().identifier;
    if (identifier.empty()) {
        // Try ASIN
        identifier = GetEXTHString(EXTHRecordType::ASIN);
    }
    
    if (!identifier.empty()) {
        font.data = KF8FontHandler::Deobfuscate(
            font.data.data(), font.data.size(), identifier);
        font.isObfuscated = false;
        return true;
    }
    
    return false;
}

// ============================================================================
// NAVIGATION
// ============================================================================

void AZW3Engine::BuildKF8Navigation() {
    // Use NCX if available, otherwise use guide or fragment structure
    
    if (!kf8Data.ncx.empty()) {
        // NCX provides best navigation
        // Already parsed in ParseNCX()
        return;
    }
    
    // Build from guide items
    if (!kf8Data.guide.empty()) {
        for (const auto& item : kf8Data.guide) {
            if (item.type == "toc" || item.type == "text") {
                EBookTOCEntry entry;
                entry.title = item.title;
                entry.pageNumber = 1; // Would need to calculate
                entry.level = 0;
                kf8Data.ncx.push_back(entry);
            }
        }
    }
    
    // Fallback: build from HTML headings in fragments
    if (kf8Data.ncx.empty()) {
        std::regex headingRegex(R"(<h([1-3])[^>]*>([^<]+)</h\1>)", std::regex::icase);
        
        int entryNum = 0;
        for (const auto& fragment : kf8Data.fragments) {
            std::string::const_iterator searchStart = fragment.content.cbegin();
            std::smatch match;
            
            while (std::regex_search(searchStart, fragment.content.cend(), match, headingRegex)) {
                EBookTOCEntry entry;
                entry.title = match[2].str();
                entry.level = std::stoi(match[1].str()) - 1;
                entry.pageNumber = ++entryNum;
                
                kf8Data.ncx.push_back(entry);
                searchStart = match.suffix().first;
            }
        }
    }
}

void AZW3Engine::CalculateKF8Pages() {
    // Calculate pages based on KF8 content
    
    const int CHARS_PER_PAGE = 2000;
    size_t contentLength = kf8Data.htmlContent.length();
    
    // Use parent's page count or calculate from content
    if (contentLength > 0) {
        // Override page count
        // This would be set via protected member in real implementation
    }
}

// ============================================================================
// CONTENT ACCESS
// ============================================================================

const KF8ParsedData& AZW3Engine::GetKF8Data() const {
    return kf8Data;
}

std::string AZW3Engine::GetKF8HTMLContent() const {
    return kf8Data.htmlContent;
}

std::string AZW3Engine::GetKF8CSSContent() const {
    return kf8Data.cssContent;
}

const std::vector<KF8Font>& AZW3Engine::GetEmbeddedFonts() const {
    return kf8Data.fonts;
}

int AZW3Engine::GetFontCount() const {
    return static_cast<int>(kf8Data.fonts.size());
}

const KF8Font* AZW3Engine::GetFont(int index) const {
    if (index < 0 || index >= static_cast<int>(kf8Data.fonts.size())) {
        return nullptr;
    }
    return &kf8Data.fonts[index];
}

bool AZW3Engine::HasKF8Content() const {
    return kf8Data.hasKF8Content;
}

bool AZW3Engine::HasEmbeddedFonts() const {
    return !kf8Data.fonts.empty();
}

bool AZW3Engine::HasMOBIFallback() const {
    // MOBI content is always available (parsed by parent)
    return !GetParsedData().textContent.empty();
}

std::string AZW3Engine::GetMOBIContent() const {
    return GetParsedData().textContent;
}

std::string AZW3Engine::GetPageContent(int pageNumber) const {
    if (preferKF8 && kf8Data.hasKF8Content) {
        // Return KF8 content
        // For now, return portion of HTML content
        int charsPerPage = 2000;
        int startOffset = (pageNumber - 1) * charsPerPage;
        
        if (startOffset < static_cast<int>(kf8Data.htmlContent.length())) {
            return kf8Data.htmlContent.substr(startOffset, charsPerPage);
        }
        return std::string();
    }
    
    // Fallback to MOBI content
    return MOBIEngine::GetPageContent(pageNumber);
}

std::vector<uint8_t> AZW3Engine::RenderPageToImage(int pageNumber,
                                                    int width, int height,
                                                    float dpi) const {
    // Rendering would use HTMLConverter with KF8 content
    // For now, delegate to parent
    return MOBIEngine::RenderPageToImage(pageNumber, width, height, dpi);
}

// ============================================================================
// HELPER METHODS
// ============================================================================

int AZW3Engine::FindRecordByType(const char* type, int startIndex) const {
    const auto& parsedData = GetParsedData();
    
    for (size_t i = startIndex; i < parsedData.records.size(); ++i) {
        const auto& record = parsedData.records[i];
        
        if (record.size() >= 4 && std::memcmp(record.data(), type, 4) == 0) {
            return static_cast<int>(i);
        }
    }
    
    return -1;
}

std::string AZW3Engine::GetRecordContent(int index) const {
    const auto& parsedData = GetParsedData();
    
    if (index < 0 || index >= static_cast<int>(parsedData.records.size())) {
        return std::string();
    }
    
    const auto& record = parsedData.records[index];
    return std::string(record.begin(), record.end());
}

uint32_t AZW3Engine::ReadVWI(const uint8_t*& ptr, const uint8_t* end) {
    // Variable-width integer (forward encoded)
    // Continues while high bit is clear
    uint32_t result = 0;
    
    while (ptr < end) {
        uint8_t byte = *ptr++;
        result = (result << 7) | (byte & 0x7F);
        
        if (byte & 0x80) {
            break; // Last byte
        }
    }
    
    return result;
}

uint32_t AZW3Engine::ReadVWIBackward(const uint8_t* end) {
    // Variable-width integer (backward encoded)
    // High bit set on first (most significant) byte
    uint32_t result = 0;
    const uint8_t* ptr = end - 1;
    int shift = 0;
    
    while (ptr >= end - 4) { // Max 4 bytes
        uint8_t byte = *ptr--;
        result |= static_cast<uint32_t>(byte & 0x7F) << shift;
        shift += 7;
        
        if (byte & 0x80) {
            break; // First byte (MSB)
        }
    }
    
    return result;
}

// ============================================================================
// KF8 DECOMPRESSOR
// ============================================================================

std::vector<uint8_t> KF8Decompressor::DecompressFragment(const uint8_t* data, size_t size) {
    // KF8 fragments may use PalmDOC compression
    return PalmDOCDecompressor::Decompress(data, size);
}

std::string KF8Decompressor::DecodeFlow(const uint8_t* data, size_t size,
                                         const std::vector<uint32_t>& positions) {
    std::string result;
    
    // Concatenate flow sections
    for (size_t i = 0; i < positions.size() - 1; ++i) {
        uint32_t start = positions[i];
        uint32_t end = positions[i + 1];
        
        if (start < size && end <= size) {
            result.append(reinterpret_cast<const char*>(data + start), end - start);
        }
    }
    
    return result;
}

// ============================================================================
// KF8 FONT HANDLER
// ============================================================================

bool KF8FontHandler::IsObfuscated(const uint8_t* data, size_t size) {
    if (size < 4) return false;
    
    // Check if font signature is valid
    // Valid TTF: 0x00010000 or "OTTO"
    // Valid WOFF: "wOFF"
    
    if ((data[0] == 0x00 && data[1] == 0x01 && data[2] == 0x00 && data[3] == 0x00) ||
        (data[0] == 'O' && data[1] == 'T' && data[2] == 'T' && data[3] == 'O') ||
        (data[0] == 'w' && data[1] == 'O' && data[2] == 'F' && data[3] == 'F')) {
        return false; // Valid font, not obfuscated
    }
    
    return true; // Likely obfuscated
}

std::vector<uint8_t> KF8FontHandler::Deobfuscate(const uint8_t* data, size_t size,
                                                   const std::string& identifier) {
    if (size == 0 || identifier.empty()) {
        return std::vector<uint8_t>(data, data + size);
    }
    
    std::vector<uint8_t> result(data, data + size);
    
    // Amazon font obfuscation XORs first 1040 bytes with SHA-1 of identifier
    // Simplified: XOR with identifier bytes
    size_t keyLen = identifier.length();
    size_t obfuscateLen = std::min(size, size_t(1040));
    
    for (size_t i = 0; i < obfuscateLen; ++i) {
        result[i] ^= static_cast<uint8_t>(identifier[i % keyLen]);
    }
    
    return result;
}

std::string KF8FontHandler::GetFontFamily(const uint8_t* data, size_t size) {
    // Parse font name from TTF/OTF name table
    // This is complex - simplified version
    
    if (size < 12) return "Unknown";
    
    // Look for name table
    // For now, return generic name
    return "Embedded Font";
}

std::string KF8FontHandler::DetectFontFormat(const uint8_t* data, size_t size) {
    if (size < 4) return "unknown";
    
    if (data[0] == 0x00 && data[1] == 0x01 && data[2] == 0x00 && data[3] == 0x00) {
        return "ttf";
    }
    if (data[0] == 'O' && data[1] == 'T' && data[2] == 'T' && data[3] == 'O') {
        return "otf";
    }
    if (data[0] == 'w' && data[1] == 'O' && data[2] == 'F' && data[3] == 'F') {
        return "woff";
    }
    if (data[0] == 'w' && data[1] == 'O' && data[2] == 'F' && data[3] == '2') {
        return "woff2";
    }
    
    return "unknown";
}

// ============================================================================
// AZW3 DETECTION UTILITIES
// ============================================================================

namespace AZW3Detection {

bool HasAZW3Extension(const std::string& filePath) {
    size_t dotPos = filePath.find_last_of('.');
    if (dotPos == std::string::npos) {
        return false;
    }
    
    std::string ext = filePath.substr(dotPos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    return ext == "azw3" || ext == "kf8";
}

bool HasKF8Content(const uint8_t* data, size_t size) {
    // Quick check for KF8 markers
    // Look for "KF8" string or MOBI version 8+
    
    if (size < 132) return false;
    
    // Check MOBI header version at offset 36 in Record 0
    // This requires parsing PDB first, so simplified check
    
    // Search for KF8 boundary or FDST record
    for (size_t i = 0; i < size - 4; ++i) {
        if (std::memcmp(data + i, "FDST", 4) == 0 ||
            std::memcmp(data + i, "SKEL", 4) == 0) {
            return true;
        }
    }
    
    return false;
}

bool HasKF8Boundary(const std::map<uint32_t, std::vector<std::string>>& exthRecords) {
    return exthRecords.find(EXTHRecordType::KF8_BOUNDARY) != exthRecords.end();
}

uint32_t GetKF8BoundaryOffset(const std::map<uint32_t, std::vector<std::string>>& exthRecords) {
    auto it = exthRecords.find(EXTHRecordType::KF8_BOUNDARY);
    if (it != exthRecords.end() && !it->second.empty() && it->second[0].size() >= 4) {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(it->second[0].data());
        return MOBIEngine::ReadBE32(data);
    }
    return 0;
}

bool IsKF8Version(uint32_t mobiVersion) {
    return mobiVersion >= 8;
}

} // namespace AZW3Detection

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

std::unique_ptr<IEBookEngine> CreateAZW3Engine() {
    return std::make_unique<AZW3Engine>();
}

void RegisterAZW3Plugin() {
    // Register with EBookEngineFactory
    // EBookEngineFactory::RegisterEngine("azw3", CreateAZW3Engine);
    // EBookEngineFactory::RegisterEngine("kf8", CreateAZW3Engine);
}

void UnregisterAZW3Plugin() {
    // Unregister from EBookEngineFactory
    // EBookEngineFactory::UnregisterEngine("azw3");
    // EBookEngineFactory::UnregisterEngine("kf8");
}

} // namespace UltraCanvas
