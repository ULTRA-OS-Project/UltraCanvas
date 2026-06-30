// Plugins/Documents/eBook/OEBEngine.cpp
// Open eBook (OEB/OEBPS) Engine Implementation
// Version: 1.0.0
// Last Modified: 2025-01-10
// Author: UltraCanvas Framework

#include "OEBEngine.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <filesystem>

// For directory operations
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define PATH_SEPARATOR '\\'
#else
#include <dirent.h>
#include <sys/stat.h>
#define PATH_SEPARATOR '/'
#endif

namespace UltraCanvas {

// ============================================================================
// OEB CSS EXTENSIONS IMPLEMENTATION
// ============================================================================

bool OEBCSSExtensions::ParseOEBProperty(const std::string& property,
                                         const std::string& value,
                                         OEBCSSExtensions& extensions) {
    if (property == "oeb-page-head") {
        extensions.pageHead = value;
        return true;
    }
    if (property == "oeb-page-foot") {
        extensions.pageFoot = value;
        return true;
    }
    if (property == "oeb-column-number") {
        try {
            extensions.columnNumber = std::stoi(value);
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

// ============================================================================
// OEB ENGINE CONSTRUCTOR & DESTRUCTOR
// ============================================================================

OEBEngine::OEBEngine()
    : EPUBEngine()
    , isDirectoryBased(false) {
    // Initialize OEB-specific state
    oebInfo.version = OEBVersion::Unknown;
    oebInfo.isPackaged = false;
}

OEBEngine::~OEBEngine() {
    // Cleanup handled by base class
    fileCache.clear();
}

// ============================================================================
// FORMAT IDENTIFICATION
// ============================================================================

EBookFormat OEBEngine::GetFormat() const {
    return EBookFormat::OEB;
}

std::string OEBEngine::GetFormatName() const {
    switch (oebInfo.version) {
        case OEBVersion::OEBPS_1_0:
            return "Open eBook 1.0";
        case OEBVersion::OEBPS_1_0_1:
            return "Open eBook 1.0.1";
        case OEBVersion::OEBPS_1_2:
            return "Open eBook 1.2";
        default:
            return "Open eBook";
    }
}

std::vector<std::string> OEBEngine::GetSupportedExtensions() const {
    return { "oeb", "opf" };
}

OEBVersion OEBEngine::GetOEBVersion() const {
    return oebInfo.version;
}

const OEBPackageInfo& OEBEngine::GetOEBPackageInfo() const {
    return oebInfo;
}

// ============================================================================
// FILE LOADING
// ============================================================================

bool OEBEngine::LoadFile(const std::string& filePath, const std::string& password) {
    // Clear previous state
    Close();
    fileCache.clear();
    isDirectoryBased = false;
    
    // Check file extension
    std::string ext = filePath.substr(filePath.find_last_of('.') + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    // Read file content
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        lastError = "Failed to open file: " + filePath;
        return false;
    }
    
    size_t fileSize = file.tellg();
    file.seekg(0);
    
    std::vector<uint8_t> data(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);
    file.close();
    
    // Check if it's a ZIP archive (packaged OEB)
    if (OEBDetection::IsZipArchive(data.data(), data.size())) {
        oebInfo.isPackaged = true;
        oebInfo.opfPath = filePath;
        
        // Use parent EPUB engine's ZIP handling
        return EPUBEngine::LoadFromMemory(data, password);
    }
    
    // Check if it's an OPF file (unpackaged OEB)
    if (ext == "opf" || OEBDetection::LooksLikeOEBPackage(data.data(), data.size())) {
        oebInfo.isPackaged = false;
        oebInfo.opfPath = filePath;
        oebInfo.basePath = GetDirectoryPath(filePath);
        
        return LoadFromDirectory(filePath);
    }
    
    lastError = "Unrecognized OEB file format";
    return false;
}

bool OEBEngine::LoadFromMemory(const std::vector<uint8_t>& data,
                                const std::string& password) {
    // Clear previous state
    Close();
    fileCache.clear();
    isDirectoryBased = false;
    
    // Check if it's a ZIP archive
    if (OEBDetection::IsZipArchive(data.data(), data.size())) {
        oebInfo.isPackaged = true;
        return EPUBEngine::LoadFromMemory(data, password);
    }
    
    // Try to parse as OPF XML directly
    if (OEBDetection::LooksLikeOEBPackage(data.data(), data.size())) {
        std::string opfContent(data.begin(), data.end());
        oebInfo.isPackaged = false;
        
        // Note: Directory-based loading from memory is limited
        // We can only parse the OPF, not load referenced files
        return ParseOEBPackage(opfContent);
    }
    
    lastError = "Unrecognized OEB data format";
    return false;
}

bool OEBEngine::LoadFromDirectory(const std::string& opfPath) {
    isDirectoryBased = true;
    baseDirectory = GetDirectoryPath(opfPath);
    
    // Read OPF file
    std::vector<uint8_t> opfData;
    if (!ReadFileFromDirectory(opfPath, opfData)) {
        lastError = "Failed to read OPF file: " + opfPath;
        return false;
    }
    
    std::string opfContent(opfData.begin(), opfData.end());
    
    // Parse OEB package
    if (!ParseOEBPackage(opfContent)) {
        return false;
    }
    
    // Load stylesheets
    LoadStylesheets();
    
    // Build navigation from spine (OEB has no NCX)
    BuildNavigationFromSpine();
    
    // Calculate pages
    CalculatePages(EBookRenderSettings());
    
    isLoaded = true;
    return true;
}

// ============================================================================
// VALIDATION
// ============================================================================

bool OEBEngine::ValidateFile(const std::string& filePath) {
    // Check extension
    if (!OEBDetection::HasOEBExtension(filePath)) {
        return false;
    }
    
    // Try to read file
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }
    
    size_t fileSize = file.tellg();
    if (fileSize == 0) {
        return false;
    }
    
    // Read first 1KB for detection
    file.seekg(0);
    size_t readSize = std::min(fileSize, size_t(1024));
    std::vector<uint8_t> header(readSize);
    file.read(reinterpret_cast<char*>(header.data()), readSize);
    file.close();
    
    // Check if ZIP or OPF XML
    return OEBDetection::IsZipArchive(header.data(), header.size()) ||
           OEBDetection::LooksLikeOEBPackage(header.data(), header.size());
}

// ============================================================================
// STATIC UTILITY METHODS
// ============================================================================

bool OEBEngine::IsOEBFile(const std::string& filePath) {
    return OEBDetection::HasOEBExtension(filePath);
}

bool OEBEngine::IsOEBData(const uint8_t* data, size_t size) {
    return OEBDetection::IsZipArchive(data, size) ||
           OEBDetection::LooksLikeOEBPackage(data, size);
}

std::string OEBEngine::NormalizeMimeType(const std::string& oebMimeType) {
    // Convert OEB-specific MIME types to standard equivalents
    if (oebMimeType == OEBMimeTypes::OEB_DOCUMENT) {
        return "application/xhtml+xml";
    }
    if (oebMimeType == OEBMimeTypes::OEB_CSS) {
        return "text/css";
    }
    if (oebMimeType == OEBMimeTypes::OEB_PACKAGE) {
        return "application/oebps-package+xml";
    }
    
    // Return as-is if not OEB-specific
    return oebMimeType;
}

OEBCSSExtensions OEBEngine::ParseOEBCSS(const std::string& cssContent) {
    return OEBCSSParser::Parse(cssContent);
}

// ============================================================================
// PROTECTED OVERRIDE METHODS
// ============================================================================

bool OEBEngine::ParseOPF() {
    // For packaged OEB, delegate to parent's OPF parsing
    // but with OEB-specific handling
    
    if (oebInfo.isPackaged) {
        // Call parent's ParseOPF
        if (!EPUBEngine::ParseOPF()) {
            return false;
        }
        
        // Detect OEB version from parsed package
        // (This would examine the package/@version attribute)
        
        return true;
    }
    
    // For directory-based, parsing happens in LoadFromDirectory
    return true;
}

bool OEBEngine::ParseOEBPackage(const std::string& opfContent) {
    // Detect OEB version
    oebInfo.version = DetectOEBVersion(opfContent);
    
    // Use TinyXML2 or similar for XML parsing
    // For now, use simple regex-based extraction
    
    // Extract metadata
    std::regex titleRegex(R"(<dc:title[^>]*>([^<]+)</dc:title>)", std::regex::icase);
    std::regex creatorRegex(R"(<dc:creator[^>]*>([^<]+)</dc:creator>)", std::regex::icase);
    std::regex identifierRegex(R"(<dc:identifier[^>]*>([^<]+)</dc:identifier>)", std::regex::icase);
    std::regex languageRegex(R"(<dc:language[^>]*>([^<]+)</dc:language>)", std::regex::icase);
    std::regex publisherRegex(R"(<dc:publisher[^>]*>([^<]+)</dc:publisher>)", std::regex::icase);
    std::regex descriptionRegex(R"(<dc:description[^>]*>([^<]+)</dc:description>)", std::regex::icase);
    
    std::smatch match;
    
    if (std::regex_search(opfContent, match, titleRegex)) {
        metadata.title = match[1].str();
    }
    if (std::regex_search(opfContent, match, creatorRegex)) {
        metadata.author = match[1].str();
        metadata.creators.push_back(match[1].str());
    }
    if (std::regex_search(opfContent, match, identifierRegex)) {
        metadata.identifier = match[1].str();
    }
    if (std::regex_search(opfContent, match, languageRegex)) {
        metadata.language = match[1].str();
    }
    if (std::regex_search(opfContent, match, publisherRegex)) {
        metadata.publisher = match[1].str();
    }
    if (std::regex_search(opfContent, match, descriptionRegex)) {
        metadata.description = match[1].str();
    }
    
    // Extract manifest items
    std::regex manifestItemRegex(
        R"(<item\s+[^>]*id\s*=\s*"([^"]+)"[^>]*href\s*=\s*"([^"]+)"[^>]*media-type\s*=\s*"([^"]+)"[^>]*/?>)",
        std::regex::icase);
    
    std::string::const_iterator searchStart = opfContent.cbegin();
    while (std::regex_search(searchStart, opfContent.cend(), match, manifestItemRegex)) {
        EPUBManifestItem item;
        item.id = match[1].str();
        item.href = match[2].str();
        item.mediaType = TranslateMimeType(match[3].str());
        
        epubPackage.manifest[item.id] = item;
        searchStart = match.suffix().first;
    }
    
    // Extract spine
    std::regex spineItemRegex(R"(<itemref\s+[^>]*idref\s*=\s*"([^"]+)"[^>]*/?>)", std::regex::icase);
    searchStart = opfContent.cbegin();
    while (std::regex_search(searchStart, opfContent.cend(), match, spineItemRegex)) {
        EPUBSpineItem spineItem;
        spineItem.idref = match[1].str();
        spineItem.linear = true;  // Default
        
        epubPackage.spine.push_back(spineItem);
        searchStart = match.suffix().first;
    }
    
    // Extract package version
    std::regex versionRegex(R"(<package[^>]*version\s*=\s*"([^"]+)")", std::regex::icase);
    if (std::regex_search(opfContent, match, versionRegex)) {
        oebInfo.specificationVersion = match[1].str();
    }
    
    return !epubPackage.spine.empty();
}

OEBVersion OEBEngine::DetectOEBVersion(const std::string& opfContent) {
    // Check for version attribute in package element
    std::regex versionRegex(R"(<package[^>]*version\s*=\s*"([^"]+)")", std::regex::icase);
    std::smatch match;
    
    if (std::regex_search(opfContent, match, versionRegex)) {
        std::string version = match[1].str();
        
        if (version.find("1.2") != std::string::npos) {
            return OEBVersion::OEBPS_1_2;
        }
        if (version.find("1.0.1") != std::string::npos) {
            return OEBVersion::OEBPS_1_0_1;
        }
        if (version.find("1.0") != std::string::npos) {
            return OEBVersion::OEBPS_1_0;
        }
    }
    
    // Check for OEBPS namespace
    if (opfContent.find("http://openebook.org/namespaces/oeb-package/1.0") != std::string::npos) {
        return OEBVersion::OEBPS_1_0;
    }
    
    // Default to 1.2 if can't determine
    return OEBVersion::OEBPS_1_2;
}

std::string OEBEngine::TranslateMimeType(const std::string& mimeType) const {
    return NormalizeMimeType(mimeType);
}

bool OEBEngine::LoadContentDocument(const std::string& href, std::string& content) {
    if (isDirectoryBased) {
        // Load from directory
        std::string fullPath = ResolvePath(href);
        std::vector<uint8_t> data;
        
        if (!ReadFileFromDirectory(fullPath, data)) {
            return false;
        }
        
        content.assign(data.begin(), data.end());
        return true;
    }
    
    // Use parent's ZIP-based loading
    return EPUBEngine::LoadContentDocument(href, content);
}

bool OEBEngine::LoadStylesheets() {
    // Load stylesheets
    if (!EPUBEngine::LoadStylesheets()) {
        // Not a failure - stylesheets are optional
    }
    
    // Process OEB CSS extensions
    ProcessOEBStylesheets();
    
    return true;
}

void OEBEngine::ProcessOEBStylesheets() {
    // Parse OEB-specific CSS properties from loaded stylesheets
    for (const auto& [id, content] : stylesheetContent) {
        OEBCSSExtensions extensions = OEBCSSParser::Parse(content);
        
        // Merge with global extensions (later stylesheets override)
        if (!extensions.pageHead.empty()) {
            globalCSSExtensions.pageHead = extensions.pageHead;
        }
        if (!extensions.pageFoot.empty()) {
            globalCSSExtensions.pageFoot = extensions.pageFoot;
        }
        if (extensions.columnNumber > 1) {
            globalCSSExtensions.columnNumber = extensions.columnNumber;
        }
    }
}

void OEBEngine::BuildNavigationFromSpine() {
    // OEB doesn't have NCX, so build navigation from spine
    tableOfContents.clear();
    
    int pageNumber = 1;
    for (size_t i = 0; i < epubPackage.spine.size(); ++i) {
        const auto& spineItem = epubPackage.spine[i];
        const auto* manifestItem = epubPackage.GetManifestItem(spineItem.idref);
        
        if (manifestItem) {
            EBookTOCEntry entry;
            entry.title = "Chapter " + std::to_string(i + 1);
            entry.href = manifestItem->href;
            entry.pageNumber = pageNumber;
            entry.level = 0;
            
            // Try to extract title from document
            std::string content;
            if (LoadContentDocument(manifestItem->href, content)) {
                // Extract <title> or first <h1>
                std::regex titleRegex(R"(<title[^>]*>([^<]+)</title>)", std::regex::icase);
                std::regex h1Regex(R"(<h1[^>]*>([^<]+)</h1>)", std::regex::icase);
                std::smatch match;
                
                if (std::regex_search(content, match, titleRegex)) {
                    entry.title = match[1].str();
                } else if (std::regex_search(content, match, h1Regex)) {
                    entry.title = match[1].str();
                }
            }
            
            tableOfContents.push_back(entry);
            ++pageNumber;  // Simplified: one spine item = one page
        }
    }
}

// ============================================================================
// DIRECTORY-BASED FILE ACCESS
// ============================================================================

bool OEBEngine::ReadFileFromDirectory(const std::string& relativePath,
                                       std::vector<uint8_t>& data) {
    // Check cache first
    auto it = fileCache.find(relativePath);
    if (it != fileCache.end()) {
        data = it->second;
        return true;
    }
    
    // Resolve full path
    std::string fullPath = ResolvePath(relativePath);
    
    // Read file
    std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }
    
    size_t fileSize = file.tellg();
    file.seekg(0);
    
    data.resize(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);
    file.close();
    
    // Cache for future access
    fileCache[relativePath] = data;
    
    return true;
}

std::string OEBEngine::ResolvePath(const std::string& relativePath) const {
    if (baseDirectory.empty()) {
        return relativePath;
    }
    
    // Handle absolute paths
    if (!relativePath.empty() && (relativePath[0] == '/' || relativePath[0] == '\\')) {
        return relativePath;
    }
    
    // Handle Windows absolute paths (C:\...)
    if (relativePath.length() >= 2 && relativePath[1] == ':') {
        return relativePath;
    }
    
    // Combine base directory with relative path
    std::string result = baseDirectory;
    if (result.back() != '/' && result.back() != '\\') {
        result += PATH_SEPARATOR;
    }
    result += NormalizePath(relativePath);
    
    return result;
}

bool OEBEngine::FileExistsInDirectory(const std::string& relativePath) const {
    std::string fullPath = ResolvePath(relativePath);
    std::ifstream file(fullPath);
    return file.good();
}

std::string OEBEngine::GetDirectoryPath(const std::string& filePath) {
    size_t lastSep = filePath.find_last_of("/\\");
    if (lastSep != std::string::npos) {
        return filePath.substr(0, lastSep);
    }
    return ".";
}

std::string OEBEngine::NormalizePath(const std::string& path) {
    std::string result = path;
    
    // Convert all separators to native
    for (char& c : result) {
        if (c == '/' || c == '\\') {
            c = PATH_SEPARATOR;
        }
    }
    
    return result;
}

// ============================================================================
// OEB CSS PARSER IMPLEMENTATION
// ============================================================================

OEBCSSExtensions OEBCSSParser::Parse(const std::string& cssContent) {
    OEBCSSExtensions extensions;
    
    // Find OEB-specific properties
    std::regex oebPropertyRegex(
        R"(oeb-(page-head|page-foot|column-number)\s*:\s*([^;}\n]+))",
        std::regex::icase);
    
    std::string::const_iterator searchStart = cssContent.cbegin();
    std::smatch match;
    
    while (std::regex_search(searchStart, cssContent.cend(), match, oebPropertyRegex)) {
        std::string property = "oeb-" + match[1].str();
        std::string value = match[2].str();
        
        // Trim whitespace
        value.erase(0, value.find_first_not_of(" \t\n\r"));
        value.erase(value.find_last_not_of(" \t\n\r") + 1);
        
        // Remove quotes if present
        if (value.length() >= 2 && 
            ((value.front() == '"' && value.back() == '"') ||
             (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.length() - 2);
        }
        
        OEBCSSExtensions::ParseOEBProperty(property, value, extensions);
        searchStart = match.suffix().first;
    }
    
    return extensions;
}

bool OEBCSSParser::IsOEBProperty(const std::string& propertyName) {
    return propertyName == "oeb-page-head" ||
           propertyName == "oeb-page-foot" ||
           propertyName == "oeb-column-number";
}

std::string OEBCSSParser::ConvertToStandardCSS(const std::string& oebCSS) {
    std::string result = oebCSS;
    
    // Remove OEB-specific properties (they're not valid CSS)
    std::regex oebPropertyRegex(
        R"(oeb-(page-head|page-foot|column-number)\s*:\s*[^;}\n]+;?)",
        std::regex::icase);
    
    result = std::regex_replace(result, oebPropertyRegex, "");
    
    // Convert oeb-column-number to CSS columns (approximation)
    // This would need to be done before removal if we want to convert
    
    return result;
}

std::string OEBCSSParser::ParsePageHead(const std::string& value) {
    return value;
}

std::string OEBCSSParser::ParsePageFoot(const std::string& value) {
    return value;
}

int OEBCSSParser::ParseColumnNumber(const std::string& value) {
    try {
        return std::stoi(value);
    } catch (...) {
        return 1;
    }
}

// ============================================================================
// OEB DETECTION UTILITIES
// ============================================================================

namespace OEBDetection {

bool HasOEBExtension(const std::string& filePath) {
    size_t dotPos = filePath.find_last_of('.');
    if (dotPos == std::string::npos) {
        return false;
    }
    
    std::string ext = filePath.substr(dotPos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    return ext == "oeb" || ext == "opf";
}

bool LooksLikeOEBPackage(const uint8_t* data, size_t size) {
    if (size < 50) {
        return false;
    }
    
    // Convert to string for easier searching
    std::string content(reinterpret_cast<const char*>(data), 
                        std::min(size, size_t(1024)));
    
    // Check for XML declaration and package element
    bool hasXml = content.find("<?xml") != std::string::npos;
    bool hasPackage = content.find("<package") != std::string::npos;
    
    // Check for OEB-specific namespaces or MIME types
    bool hasOebNamespace = 
        content.find("openebook.org") != std::string::npos ||
        content.find("idpf.org") != std::string::npos ||
        content.find("oebps") != std::string::npos ||
        content.find("text/x-oeb") != std::string::npos;
    
    return hasXml && hasPackage && (hasOebNamespace || 
           content.find("<manifest") != std::string::npos);
}

bool IsZipArchive(const uint8_t* data, size_t size) {
    if (size < 4) {
        return false;
    }
    
    // ZIP files start with "PK\x03\x04" or "PK\x05\x06" (empty) or "PK\x07\x08" (spanned)
    return data[0] == 'P' && data[1] == 'K' &&
           ((data[2] == 0x03 && data[3] == 0x04) ||
            (data[2] == 0x05 && data[3] == 0x06) ||
            (data[2] == 0x07 && data[3] == 0x08));
}

std::string DetectEBookTypeInZip(const uint8_t* data, size_t size) {
    // This would need full ZIP parsing
    // For now, return empty and let loader try EPUB first
    return "";
}

std::string FindOPFInDirectory(const std::string& directory) {
    // Look for .opf files in directory
    std::vector<std::string> candidates = {
        "content.opf",
        "package.opf",
        "book.opf"
    };
    
    for (const auto& candidate : candidates) {
        std::string path = directory;
        if (path.back() != '/' && path.back() != '\\') {
            path += PATH_SEPARATOR;
        }
        path += candidate;
        
        std::ifstream file(path);
        if (file.good()) {
            return path;
        }
    }
    
    // Search for any .opf file
#ifdef _WIN32
    WIN32_FIND_DATAA findData;
    std::string searchPath = directory + "\\*.opf";
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        std::string result = directory + "\\" + findData.cFileName;
        FindClose(hFind);
        return result;
    }
#else
    DIR* dir = opendir(directory.c_str());
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name.length() > 4 && 
                name.substr(name.length() - 4) == ".opf") {
                std::string result = directory + "/" + name;
                closedir(dir);
                return result;
            }
        }
        closedir(dir);
    }
#endif
    
    return "";
}

} // namespace OEBDetection

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

std::unique_ptr<IEBookEngine> CreateOEBEngine() {
    return std::make_unique<OEBEngine>();
}

void RegisterOEBPlugin() {
    // Register with EBookEngineFactory
    // EBookEngineFactory::RegisterEngine("oeb", CreateOEBEngine);
    // EBookEngineFactory::RegisterEngine("opf", CreateOEBEngine);
}

void UnregisterOEBPlugin() {
    // Unregister from EBookEngineFactory
    // EBookEngineFactory::UnregisterEngine("oeb");
    // EBookEngineFactory::UnregisterEngine("opf");
}

} // namespace UltraCanvas
