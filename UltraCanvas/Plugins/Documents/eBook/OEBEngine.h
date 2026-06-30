// Plugins/Documents/eBook/OEBEngine.h
// Open eBook (OEB/OEBPS) Engine - Legacy format support via EPUBEngine extension
// Version: 1.0.0
// Last Modified: 2025-01-10
// Author: UltraCanvas Framework
#pragma once

#include "EPUBEngine.h"
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace UltraCanvas {

// ============================================================================
// OEB FORMAT INFORMATION
// ============================================================================
//
// Open eBook Publication Structure (OEBPS) is a legacy eBook format that was
// superseded by EPUB in September 2007. OEB shares most of its structure with
// EPUB, making it possible to handle OEB files with minor modifications to
// the EPUB engine.
//
// Key Differences from EPUB:
// 1. May not be packaged as ZIP (can be loose files with .opf manifest)
// 2. Uses older MIME types: text/x-oeb1-document, text/x-oeb1-css
// 3. Has OEB-specific CSS properties: oeb-page-head, oeb-page-foot, oeb-column-number
// 4. No NCX navigation document (EPUB 2 addition)
// 5. No container.xml (EPUB OCF addition)
// 6. Uses OEBPS 1.0/1.0.1/1.2 vs EPUB's OPS 2.0+
//
// File Extensions: .oeb, .opf
// MIME Type: application/oebps-package+xml (for .opf package file)
//
// ============================================================================

// ===== OEB VERSION ENUM =====
enum class OEBVersion {
    Unknown = 0,
    OEBPS_1_0,      // Original 1999 specification
    OEBPS_1_0_1,    // July 2001 update
    OEBPS_1_2       // August 2002 - Final OEB version
};

// ===== OEB-SPECIFIC MIME TYPES =====
namespace OEBMimeTypes {
    // OEB 1.x MIME types (legacy)
    constexpr const char* OEB_DOCUMENT = "text/x-oeb1-document";
    constexpr const char* OEB_CSS = "text/x-oeb1-css";
    constexpr const char* OEB_PACKAGE = "application/oebps-package+xml";
    
    // Standard types also accepted
    constexpr const char* XHTML = "application/xhtml+xml";
    constexpr const char* HTML = "text/html";
    constexpr const char* CSS = "text/css";
    constexpr const char* XML = "application/xml";
}

// ===== OEB-SPECIFIC CSS PROPERTIES =====
// These are OEB extensions to CSS that may appear in legacy documents
struct OEBCSSExtensions {
    // Page header/footer content (similar to CSS @page)
    std::string pageHead;           // oeb-page-head
    std::string pageFoot;           // oeb-page-foot
    
    // Column layout
    int columnNumber = 1;           // oeb-column-number
    
    // Parse OEB CSS extension from property string
    static bool ParseOEBProperty(const std::string& property, 
                                  const std::string& value,
                                  OEBCSSExtensions& extensions);
};

// ===== OEB PACKAGE INFO =====
struct OEBPackageInfo {
    OEBVersion version = OEBVersion::Unknown;
    std::string specificationVersion;   // e.g., "1.2"
    bool isPackaged = false;            // true if ZIP, false if loose files
    std::string basePath;               // Base path for loose file structure
    std::string opfPath;                // Path to .opf file
    
    // Legacy metadata fields
    std::string dcFormat;               // dc:format value
    std::string oebpsVersion;           // From package/@version attribute
};

// ============================================================================
// OEB ENGINE CLASS
// ============================================================================
// 
// OEBEngine extends EPUBEngine to handle legacy OEB/OEBPS files.
// Since OEB is essentially a predecessor to EPUB with very similar structure,
// we inherit most functionality and override only what differs.
//
// ============================================================================

class OEBEngine : public EPUBEngine {
public:
    // ===== CONSTRUCTOR & DESTRUCTOR =====
    OEBEngine();
    virtual ~OEBEngine();
    
    // ===== IEBookEngine INTERFACE OVERRIDES =====
    
    // Format identification
    EBookFormat GetFormat() const override;
    std::string GetFormatName() const override;
    std::vector<std::string> GetSupportedExtensions() const override;
    
    // Loading - handles both packaged (ZIP) and unpackaged OEB
    bool LoadFile(const std::string& filePath, 
                  const std::string& password = "") override;
    bool LoadFromMemory(const std::vector<uint8_t>& data,
                        const std::string& password = "") override;
    
    // Validation
    bool ValidateFile(const std::string& filePath) override;
    
    // ===== OEB-SPECIFIC METHODS =====
    
    // Get OEB version information
    OEBVersion GetOEBVersion() const;
    const OEBPackageInfo& GetOEBPackageInfo() const;
    
    // Check if file is OEB format (static utility)
    static bool IsOEBFile(const std::string& filePath);
    static bool IsOEBData(const uint8_t* data, size_t size);
    
    // Convert OEB MIME type to standard MIME type
    static std::string NormalizeMimeType(const std::string& oebMimeType);
    
    // Parse OEB-specific CSS extensions
    static OEBCSSExtensions ParseOEBCSS(const std::string& cssContent);
    
protected:
    // ===== OVERRIDE PROTECTED METHODS =====
    
    // Package parsing - handles OEB-specific package format
    bool ParseOPF() override;
    
    // Content loading - handles OEB MIME types
    bool LoadContentDocument(const std::string& href, 
                             std::string& content) override;
    
    // Stylesheet handling - processes OEB CSS extensions
    bool LoadStylesheets() override;
    
    // ===== OEB-SPECIFIC PARSING =====
    
    // Load from unpackaged directory structure
    bool LoadFromDirectory(const std::string& opfPath);
    
    // Parse OEB package file (.opf)
    bool ParseOEBPackage(const std::string& opfContent);
    
    // Detect OEB version from package
    OEBVersion DetectOEBVersion(const std::string& opfContent);
    
    // Handle legacy manifest MIME types
    std::string TranslateMimeType(const std::string& mimeType) const;
    
    // Process OEB CSS extensions in stylesheets
    void ProcessOEBStylesheets();
    
    // Build navigation from spine (OEB has no NCX)
    void BuildNavigationFromSpine();
    
private:
    // ===== OEB-SPECIFIC STATE =====
    OEBPackageInfo oebInfo;
    OEBCSSExtensions globalCSSExtensions;
    
    // Directory-based loading
    std::string baseDirectory;
    bool isDirectoryBased = false;
    
    // File cache for directory-based loading
    std::map<std::string, std::vector<uint8_t>> fileCache;
    
    // ===== HELPER METHODS =====
    
    // Read file from directory (for unpackaged OEB)
    bool ReadFileFromDirectory(const std::string& relativePath,
                               std::vector<uint8_t>& data);
    
    // Resolve relative path against base directory
    std::string ResolvePath(const std::string& relativePath) const;
    
    // Check if path exists in directory structure
    bool FileExistsInDirectory(const std::string& relativePath) const;
    
    // Extract directory from file path
    static std::string GetDirectoryPath(const std::string& filePath);
    
    // Normalize path separators
    static std::string NormalizePath(const std::string& path);
};

// ============================================================================
// OEB CSS EXTENSION PARSER
// ============================================================================

class OEBCSSParser {
public:
    // Parse OEB-specific CSS properties from stylesheet
    static OEBCSSExtensions Parse(const std::string& cssContent);
    
    // Check if property is OEB-specific
    static bool IsOEBProperty(const std::string& propertyName);
    
    // Convert OEB CSS to standard CSS where possible
    static std::string ConvertToStandardCSS(const std::string& oebCSS);
    
private:
    // Parse oeb-page-head property
    static std::string ParsePageHead(const std::string& value);
    
    // Parse oeb-page-foot property
    static std::string ParsePageFoot(const std::string& value);
    
    // Parse oeb-column-number property
    static int ParseColumnNumber(const std::string& value);
};

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

// Create OEB engine instance
std::unique_ptr<IEBookEngine> CreateOEBEngine();

// Register OEB format with engine factory
void RegisterOEBPlugin();
void UnregisterOEBPlugin();

// ============================================================================
// OEB FILE DETECTION UTILITIES
// ============================================================================

namespace OEBDetection {
    // Check if file has OEB extension
    bool HasOEBExtension(const std::string& filePath);
    
    // Check if data looks like OEB package (XML with package element)
    bool LooksLikeOEBPackage(const uint8_t* data, size_t size);
    
    // Check if data is ZIP (packaged OEB)
    bool IsZipArchive(const uint8_t* data, size_t size);
    
    // Detect if ZIP contains OEB or EPUB
    // Returns: "oeb", "epub", or empty string
    std::string DetectEBookTypeInZip(const uint8_t* data, size_t size);
    
    // Find .opf file in directory
    std::string FindOPFInDirectory(const std::string& directory);
}

} // namespace UltraCanvas

/*
=============================================================================
OEB ENGINE USAGE EXAMPLES
=============================================================================

// Example 1: Load packaged OEB file (ZIP format)
auto oebEngine = UltraCanvas::CreateOEBEngine();
if (oebEngine->LoadFile("/path/to/book.oeb")) {
    auto metadata = oebEngine->GetMetadata();
    std::cout << "Title: " << metadata.title << std::endl;
    std::cout << "OEB Version: " << oebEngine->GetOEBPackageInfo().specificationVersion << std::endl;
}

// Example 2: Load unpackaged OEB (directory with .opf)
auto oebEngine = UltraCanvas::CreateOEBEngine();
if (oebEngine->LoadFile("/path/to/book/content.opf")) {
    // Engine detects directory structure and loads accordingly
    int pageCount = oebEngine->GetPageCount();
}

// Example 3: Detect OEB vs EPUB
if (UltraCanvas::OEBEngine::IsOEBFile("/path/to/file.opf")) {
    auto engine = UltraCanvas::CreateOEBEngine();
    engine->LoadFile("/path/to/file.opf");
} else {
    auto engine = UltraCanvas::CreateEPUBEngine();
    engine->LoadFile("/path/to/file.epub");
}

// Example 4: Convert OEB MIME types
std::string oebMime = "text/x-oeb1-document";
std::string standardMime = UltraCanvas::OEBEngine::NormalizeMimeType(oebMime);
// standardMime = "application/xhtml+xml"

// Example 5: Parse OEB CSS extensions
std::string oebCSS = "body { oeb-column-number: 2; oeb-page-head: 'Chapter Title'; }";
auto extensions = UltraCanvas::OEBCSSParser::Parse(oebCSS);
std::cout << "Columns: " << extensions.columnNumber << std::endl;
std::cout << "Header: " << extensions.pageHead << std::endl;

=============================================================================
OEB FORMAT SUPPORT MATRIX
=============================================================================

| Feature                    | OEB 1.0 | OEB 1.0.1 | OEB 1.2 | Notes           |
|---------------------------|---------|-----------|---------|-----------------|
| XML Package (.opf)        | ✅      | ✅        | ✅      | Core feature    |
| XHTML Content             | ✅      | ✅        | ✅      | Subset of XHTML |
| CSS Styling               | ✅      | ✅        | ✅      | CSS 2 subset    |
| Dublin Core Metadata      | ✅      | ✅        | ✅      | Required        |
| ZIP Packaging             | ⚠️      | ⚠️        | ✅      | Optional        |
| Unpackaged (directory)    | ✅      | ✅        | ✅      | Supported       |
| OEB CSS Extensions        | ✅      | ✅        | ✅      | oeb-page-*, etc |
| JPEG Images               | ✅      | ✅        | ✅      | Required format |
| PNG Images                | ✅      | ✅        | ✅      | Required format |
| GIF Images                | ❌      | ✅        | ✅      | Added in 1.0.1  |
| SVG Images                | ❌      | ❌        | ✅      | Added in 1.2    |
| NCX Navigation            | ❌      | ❌        | ❌      | EPUB 2 feature  |
| DRM                       | ⚠️      | ⚠️        | ⚠️      | Implementation  |
|                           |         |           |         | specific        |

Legend: ✅ Supported | ⚠️ Partial/Optional | ❌ Not available

=============================================================================
*/
