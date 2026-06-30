// Plugins/Documents/eBook/AZW3Engine.h
// Amazon Kindle Format 8 (KF8/AZW3) eBook Engine
// Version: 1.0.0
// Last Modified: 2025-01-10
// Author: UltraCanvas Framework
#pragma once

#include "MOBIEngine.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>

namespace UltraCanvas {

// ============================================================================
// AZW3/KF8 FORMAT INFORMATION
// ============================================================================
//
// AZW3 (Amazon Kindle Format 8 / KF8) is Amazon's advanced eBook format
// introduced in 2011 with the Kindle Fire. It represents a significant
// upgrade over the original AZW/MOBI formats.
//
// Key Features:
// - Based on PDB structure (like MOBI)
// - Supports HTML5 and CSS3
// - Embedded fonts
// - SVG graphics
// - Advanced layout features (drop caps, floats, tables)
// - Can contain both MOBI (for older devices) and KF8 content
//
// File Extensions: .azw3, .azw (sometimes)
// MIME Type: application/vnd.amazon.mobi8-ebook
//
// Structure:
// 1. PDB Header (same as MOBI)
// 2. MOBI content (for backward compatibility)
// 3. KF8 Boundary marker (EXTH record 121)
// 4. KF8 content (HTML5/CSS3-based)
// 5. Shared resources (images, fonts)
//
// ============================================================================

// ===== KF8-SPECIFIC RECORD TYPES =====
namespace KF8RecordType {
    // Fragment records
    constexpr const char* FDST = "FDST";    // Fragment Data Start Table
    constexpr const char* SKEL = "SKEL";    // Skeleton (structure)
    constexpr const char* FRAG = "FRAG";    // Fragment
    constexpr const char* GUIDE = "GUID";   // Guide items
    constexpr const char* NCXC = "NCXC";    // NCX (Navigation)
    
    // Resource records
    constexpr const char* RESC = "RESC";    // Resource record
    constexpr const char* FONT = "FONT";    // Embedded font
    constexpr const char* CRES = "CRES";    // Container resource
    
    // Index records
    constexpr const char* INDX = "INDX";    // Index header
    constexpr const char* TAGX = "TAGX";    // Tag index
    constexpr const char* IDXT = "IDXT";    // ID index table
    
    // Boundary markers
    constexpr const char* BOUNDARY = "BOUNDARY"; // KF8 boundary
}

// ===== KF8 FRAGMENT STRUCTURE =====
struct KF8Fragment {
    uint32_t index;             // Fragment index
    uint32_t filePosition;      // Position in file
    uint32_t length;            // Fragment length
    std::string content;        // HTML content
    std::string selector;       // CSS selector for this fragment
};

// ===== KF8 SKELETON STRUCTURE =====
struct KF8Skeleton {
    uint32_t index;
    std::string name;           // Skeleton name/ID
    uint32_t fragmentCount;     // Number of fragments
    std::vector<uint32_t> fragmentIds;  // Fragment indices
};

// ===== KF8 RESOURCE STRUCTURE =====
struct KF8Resource {
    uint32_t index;
    std::string type;           // "image", "font", "css", etc.
    std::string mediaType;      // MIME type
    std::string href;           // Original href/path
    std::vector<uint8_t> data;  // Resource data
};

// ===== KF8 FONT RECORD =====
struct KF8Font {
    uint32_t index;
    std::string fontFamily;     // Font family name
    std::string fontStyle;      // normal, italic
    std::string fontWeight;     // normal, bold, 100-900
    std::vector<uint8_t> data;  // Font data (TTF/OTF/WOFF)
    bool isObfuscated = false;  // Font obfuscation flag
};

// ===== KF8 GUIDE ITEM =====
struct KF8GuideItem {
    std::string type;           // "toc", "cover", "text", etc.
    std::string title;
    uint32_t filePosition;
};

// ===== FDST (Fragment Data Start Table) =====
#pragma pack(push, 1)
struct FDSTHeader {
    char identifier[4];         // "FDST"
    uint32_t headerLength;      // Header length (usually 12)
    uint32_t fragmentCount;     // Number of fragments
    // Followed by fragment offsets (4 bytes each)
};
#pragma pack(pop)

// ===== KF8 INDEX HEADER =====
#pragma pack(push, 1)
struct INDXHeader {
    char identifier[4];         // "INDX"
    uint32_t headerLength;      // Header length
    uint32_t unknown1;
    uint32_t type;              // Index type
    uint32_t idxtOffset;        // Offset to IDXT
    uint32_t recordCount;       // Number of records
    uint32_t encoding;          // Text encoding
    uint32_t language;          // Language code
    uint32_t totalEntries;      // Total entries
    uint32_t ordt1Offset;       // ORDT1 offset
    uint32_t ordt2Offset;       // ORDT2 offset
    uint32_t tagxOffset;        // TAGX offset
    // More fields follow...
};
#pragma pack(pop)

// ===== KF8 PARSED DATA =====
struct KF8ParsedData {
    // KF8 boundary
    uint32_t kf8BoundaryOffset = 0;     // Record index of KF8 start
    bool hasKF8Content = false;
    
    // Fragments
    std::vector<KF8Fragment> fragments;
    std::vector<KF8Skeleton> skeletons;
    
    // Resources
    std::vector<KF8Resource> resources;
    std::vector<KF8Font> fonts;
    
    // Navigation
    std::vector<KF8GuideItem> guide;
    std::vector<EBookTOCEntry> ncx;     // Navigation Control for XML
    
    // Content
    std::string htmlContent;            // Reconstructed HTML
    std::string cssContent;             // Combined CSS
    
    // Flow information
    int flowCount = 0;
    std::vector<std::pair<uint32_t, uint32_t>> flowRanges;  // Start, length
};

// ============================================================================
// AZW3 ENGINE CLASS
// ============================================================================
//
// AZW3Engine extends MOBIEngine to handle Kindle Format 8 files.
// Since AZW3 files use the same PDB structure as MOBI, we can reuse
// most of the parsing logic and add KF8-specific handling.
//
// ============================================================================

class AZW3Engine : public MOBIEngine {
public:
    // ===== CONSTRUCTOR & DESTRUCTOR =====
    AZW3Engine();
    virtual ~AZW3Engine();
    
    // ===== IEBookEngine INTERFACE OVERRIDES =====
    
    // Format identification
    EBookFormat GetFormat() const override;
    std::string GetFormatName() const override;
    std::vector<std::string> GetSupportedExtensions() const override;
    
    // Document loading (adds KF8 parsing)
    bool LoadFile(const std::string& filePath,
                  const std::string& password = "") override;
    bool LoadFromMemory(const std::vector<uint8_t>& data,
                        const std::string& password = "") override;
    
    // Validation
    bool ValidateFile(const std::string& filePath) override;
    
    // Content access (prefer KF8 content)
    std::string GetPageContent(int pageNumber) const override;
    std::vector<uint8_t> RenderPageToImage(int pageNumber,
                                            int width, int height,
                                            float dpi) const override;
    
    // ===== AZW3-SPECIFIC METHODS =====
    
    // Get KF8 parsed data
    const KF8ParsedData& GetKF8Data() const;
    
    // Check if file is AZW3/KF8 format
    static bool IsAZW3File(const std::string& filePath);
    static bool IsAZW3Data(const uint8_t* data, size_t size);
    
    // Get KF8 content (HTML5)
    std::string GetKF8HTMLContent() const;
    
    // Get combined CSS
    std::string GetKF8CSSContent() const;
    
    // Get embedded fonts
    const std::vector<KF8Font>& GetEmbeddedFonts() const;
    int GetFontCount() const;
    const KF8Font* GetFont(int index) const;
    
    // Check format features
    bool HasKF8Content() const;
    bool HasEmbeddedFonts() const;
    bool HasMOBIFallback() const;
    
    // Get MOBI content (fallback)
    std::string GetMOBIContent() const;
    
protected:
    // ===== KF8 PARSING METHODS =====
    
    // Parse KF8 content after MOBI parsing
    bool ParseKF8Content();
    
    // Find KF8 boundary in EXTH records
    bool FindKF8Boundary();
    
    // Parse KF8-specific records
    bool ParseFDST(const std::vector<uint8_t>& record);
    bool ParseSkeleton(const std::vector<uint8_t>& record);
    bool ParseFragments();
    bool ParseResources();
    bool ParseFonts();
    bool ParseNCX();
    bool ParseGuide();
    
    // Parse index structures
    bool ParseINDX(const std::vector<uint8_t>& record);
    bool ParseTAGX(const uint8_t* data, size_t size);
    bool ParseIDXT(const uint8_t* data, size_t size);
    
    // Reconstruct HTML from fragments
    bool ReconstructHTML();
    
    // Extract embedded fonts
    bool ExtractFont(int recordIndex, KF8Font& font);
    bool DeobfuscateFont(KF8Font& font);
    
    // Build navigation from NCX
    void BuildKF8Navigation();
    
    // Override page calculation for KF8
    void CalculateKF8Pages();
    
private:
    // ===== KF8 STATE =====
    KF8ParsedData kf8Data;
    
    // Content preference
    bool preferKF8 = true;      // Prefer KF8 over MOBI content
    
    // ===== HELPER METHODS =====
    
    // Find record by type (FDST, SKEL, etc.)
    int FindRecordByType(const char* type, int startIndex = 0) const;
    
    // Get record content
    std::string GetRecordContent(int index) const;
    
    // Decompress KF8 fragment
    std::string DecompressFragment(const uint8_t* data, size_t size);
    
    // Parse variable-width integer (forward encoded)
    static uint32_t ReadVWI(const uint8_t*& ptr, const uint8_t* end);
    
    // Parse variable-width integer (backward encoded)
    static uint32_t ReadVWIBackward(const uint8_t* end);
};

// ============================================================================
// KF8 DECOMPRESSION UTILITIES
// ============================================================================

class KF8Decompressor {
public:
    // Decompress KF8 fragment
    static std::vector<uint8_t> DecompressFragment(const uint8_t* data, size_t size);
    
    // Decode flow data
    static std::string DecodeFlow(const uint8_t* data, size_t size, 
                                   const std::vector<uint32_t>& positions);
};

// ============================================================================
// KF8 FONT UTILITIES
// ============================================================================

class KF8FontHandler {
public:
    // Check if font is obfuscated
    static bool IsObfuscated(const uint8_t* data, size_t size);
    
    // Deobfuscate font using book identifier
    static std::vector<uint8_t> Deobfuscate(const uint8_t* data, size_t size,
                                             const std::string& identifier);
    
    // Get font family name from font data
    static std::string GetFontFamily(const uint8_t* data, size_t size);
    
    // Detect font format (TTF, OTF, WOFF)
    static std::string DetectFontFormat(const uint8_t* data, size_t size);
};

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

// Create AZW3 engine instance
std::unique_ptr<IEBookEngine> CreateAZW3Engine();

// Register AZW3 format with engine factory
void RegisterAZW3Plugin();
void UnregisterAZW3Plugin();

// ============================================================================
// AZW3 DETECTION UTILITIES
// ============================================================================

namespace AZW3Detection {
    // Check if file has AZW3 extension
    bool HasAZW3Extension(const std::string& filePath);
    
    // Check if MOBI file contains KF8 content
    bool HasKF8Content(const uint8_t* data, size_t size);
    
    // Check for KF8 boundary in EXTH records
    bool HasKF8Boundary(const std::map<uint32_t, std::vector<std::string>>& exthRecords);
    
    // Get KF8 boundary offset from EXTH
    uint32_t GetKF8BoundaryOffset(const std::map<uint32_t, std::vector<std::string>>& exthRecords);
    
    // Check MOBI version (KF8 = version 8+)
    bool IsKF8Version(uint32_t mobiVersion);
}

} // namespace UltraCanvas

/*
=============================================================================
AZW3 ENGINE USAGE EXAMPLES
=============================================================================

// Example 1: Load AZW3 file and check for KF8 content
auto azw3Engine = UltraCanvas::CreateAZW3Engine();
if (azw3Engine->LoadFile("/path/to/book.azw3")) {
    auto* engine = static_cast<UltraCanvas::AZW3Engine*>(azw3Engine.get());
    
    if (engine->HasKF8Content()) {
        std::cout << "KF8 content available!" << std::endl;
        std::string html = engine->GetKF8HTMLContent();
        std::string css = engine->GetKF8CSSContent();
    }
    
    if (engine->HasMOBIFallback()) {
        std::cout << "MOBI fallback available!" << std::endl;
        std::string mobiContent = engine->GetMOBIContent();
    }
}

// Example 2: Extract embedded fonts
auto azw3Engine = UltraCanvas::CreateAZW3Engine();
azw3Engine->LoadFile("/path/to/book.azw3");
auto* engine = static_cast<UltraCanvas::AZW3Engine*>(azw3Engine.get());

int fontCount = engine->GetFontCount();
std::cout << "Embedded fonts: " << fontCount << std::endl;

for (int i = 0; i < fontCount; ++i) {
    const auto* font = engine->GetFont(i);
    if (font) {
        std::cout << "Font: " << font->fontFamily 
                  << " (" << font->fontStyle << ", " << font->fontWeight << ")"
                  << std::endl;
    }
}

// Example 3: Use with EBookViewer
auto viewer = UltraCanvas::CreateEBookViewer("viewer", 0, 0, 800, 600);
viewer->LoadDocument("/path/to/book.azw3");
// Viewer automatically uses KF8 content if available

// Example 4: Check file type before loading
std::string filePath = "/path/to/book.azw3";
if (UltraCanvas::AZW3Engine::IsAZW3File(filePath)) {
    auto engine = UltraCanvas::CreateAZW3Engine();
    engine->LoadFile(filePath);
} else if (UltraCanvas::MOBIEngine::IsMOBIFile(filePath)) {
    auto engine = UltraCanvas::CreateMOBIEngine();
    engine->LoadFile(filePath);
}

=============================================================================
AZW3/KF8 FORMAT SUPPORT MATRIX
=============================================================================

| Feature                    | Support | Notes                           |
|---------------------------|---------|----------------------------------|
| PDB Structure             | ✅      | Same as MOBI                     |
| MOBI Fallback Content     | ✅      | For older devices                |
| KF8 HTML5 Content         | ✅      | Primary content                  |
| CSS3 Styling              | ✅      | Advanced layout                  |
| Embedded Fonts            | ✅      | TTF/OTF/WOFF                     |
| Font Deobfuscation        | ✅      | Amazon obfuscation               |
| SVG Graphics              | ✅      | Vector images                    |
| Fixed Layout              | ⚠️      | Partial support                  |
| Drop Caps                 | ✅      | Via CSS                          |
| Tables                    | ✅      | Nested/merged cells              |
| Floating Elements         | ✅      | Sidebars, images                 |
| EXTH Metadata             | ✅      | Extended metadata                |
| NCX Navigation            | ✅      | Table of contents                |
| Guide Items               | ✅      | Cover, TOC, text                 |
| DRM Encryption            | ❌      | Not supported (legal)            |
| KFX (Format 10)           | ❌      | Different format                 |

Legend: ✅ Supported | ⚠️ Partial | ❌ Not supported

=============================================================================
*/
