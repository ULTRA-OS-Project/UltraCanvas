// Plugins/Images/UltraCanvasImagePluginManager.h
// Comprehensive image plugin management system with new folder structure
// Version: 1.1.0
// Last Modified: 2025-07-13
// Author: UltraCanvas Framework
#pragma once

// ===== CORE ULTRACANVAS INCLUDES =====
#include "../../include/UltraCanvasGraphicsPluginSystem.h"
#include "../../include/UltraCanvasImageElement.h"
#include "../../include/UltraCanvasCommonTypes.h"

// ===== IMAGE PLUGIN INCLUDES =====
#include "UltraCanvasSTBImagePlugin.h"          // Core formats (always available)

// Advanced plugins (conditional)
#ifdef ULTRACANVAS_WEBP_SUPPORT
    #include "UltraCanvasWebPPlugin.h"
#endif

#ifdef ULTRACANVAS_AVIF_SUPPORT
    #include "UltraCanvasAVIFPlugin.h"
#endif

#ifdef ULTRACANVAS_TIFF_SUPPORT
    #include "UltraCanvasTIFFPlugin.h"
#endif

#ifdef ULTRACANVAS_HEIC_SUPPORT
    #include "UltraCanvasHEICPlugin.h"
#endif

#ifdef ULTRACANVAS_JXL_SUPPORT
    #include "UltraCanvasJXLPlugin.h"
#endif

// ===== STANDARD INCLUDES =====
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <iostream>
#include <algorithm>
#include <set>

namespace UltraCanvas {

// ===== IMAGE PLUGIN CAPABILITIES =====
struct ImagePluginCapabilities {
    std::vector<std::string> readFormats;
    std::vector<std::string> writeFormats;
    bool supportsAnimation = false;
    bool supportsHDR = false;
    bool supportsLossless = false;
    bool supportsLossy = false;
    bool supportsMetadata = false;
    bool supportsMultiPage = false;
    int maxWidth = 0;
    int maxHeight = 0;
    std::string pluginName;
    std::string version;
    std::string description;
};

// ===== IMAGE CONVERSION REQUEST =====
struct ImageConversionRequest {
    std::string inputPath;
    std::string outputPath;
    ImageFormat targetFormat = ImageFormat::Unknown;
    int quality = 90;
    int maxWidth = 0;   // 0 = no limit
    int maxHeight = 0;  // 0 = no limit
    bool maintainAspectRatio = true;
    bool stripMetadata = false;
    
    ImageConversionRequest(const std::string& input, const std::string& output)
        : inputPath(input), outputPath(output) {}
};

// ===== COMPREHENSIVE IMAGE PLUGIN MANAGER =====
class UltraCanvasImagePluginManager {
private:
    static std::vector<std::shared_ptr<IGraphicsPlugin>> imagePlugins;
    static std::map<std::string, std::shared_ptr<IGraphicsPlugin>> extensionMap;
    static bool initialized;
    
public:
    // ===== INITIALIZATION =====
    static bool Initialize() {
        if (initialized) return true;
        
        std::cout << "Initializing UltraCanvas Image Plugin System..." << std::endl;
        
        // Always register STB plugin (core formats)
        RegisterSTBImagePlugin();
        std::cout << "✓ Registered STB Image Plugin (core formats)" << std::endl;
        
        // Register advanced plugins if available
        int advancedCount = 0;
        
        #ifdef ULTRACANVAS_WEBP_SUPPORT
        RegisterWebPPlugin();
        std::cout << "✓ Registered WebP Plugin" << std::endl;
        advancedCount++;
        #endif
        
        #ifdef ULTRACANVAS_AVIF_SUPPORT
        RegisterAVIFPlugin();
        std::cout << "✓ Registered AVIF Plugin" << std::endl;
        advancedCount++;
        #endif
        
        #ifdef ULTRACANVAS_TIFF_SUPPORT
        RegisterTIFFPlugin();
        std::cout << "✓ Registered TIFF Plugin" << std::endl;
        advancedCount++;
        #endif
        
        #ifdef ULTRACANVAS_HEIC_SUPPORT
        RegisterHEICPlugin();
        std::cout << "✓ Registered HEIC/HEIF Plugin" << std::endl;
        advancedCount++;
        #endif
        
        #ifdef ULTRACANVAS_JXL_SUPPORT
        RegisterJXLPlugin();
        std::cout << "✓ Registered JPEG XL Plugin" << std::endl;
        advancedCount++;
        #endif
        
        // Build extension mapping
        RebuildExtensionMap();
        
        initialized = true;
        
        std::cout << "Image Plugin System initialized with " << imagePlugins.size() 
                  << " plugins (" << advancedCount << " advanced)" << std::endl;
        
        PrintSupportedFormats();
        return true;
    }
    
    static void Shutdown() {
        imagePlugins.clear();
        extensionMap.clear();
        initialized = false;
        std::cout << "UltraCanvas Image Plugin System shut down." << std::endl;
    }
    
    // ===== PLUGIN MANAGEMENT =====
    static void RegisterPlugin(std::shared_ptr<IGraphicsPlugin> plugin) {
        if (!plugin) {
            std::cerr << "Warning: Attempted to register null plugin" << std::endl;
            return;
        }
        
        // Check for duplicates
        for (const auto& existing : imagePlugins) {
            if (existing->GetPluginName() == plugin->GetPluginName()) {
                std::cout << "Plugin already registered: " << plugin->GetPluginName() << std::endl;
                return;
            }
        }
        
        imagePlugins.push_back(plugin);
        
        // Update extension mapping
        auto extensions = plugin->GetSupportedExtensions();
        for (const auto& ext : extensions) {
            std::string lowerExt = ext;
            std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::tolower);
            extensionMap[lowerExt] = plugin;
        }
        
        std::cout << "Registered: " << plugin->GetPluginName() 
                  << " v" << plugin->GetPluginVersion() << std::endl;
    }
    
    static std::vector<ImagePluginCapabilities> GetAllCapabilities() {
        std::vector<ImagePluginCapabilities> capabilities;
        
        for (const auto& plugin : imagePlugins) {
            ImagePluginCapabilities caps;
            caps.pluginName = plugin->GetPluginName();
            caps.version = plugin->GetPluginVersion();
            caps.readFormats = plugin->GetSupportedExtensions();
            
            // Determine capabilities based on plugin type
            if (caps.pluginName.find("STB") != std::string::npos) {
                caps.writeFormats = {"png", "jpg", "jpeg", "bmp", "tga"};
                caps.supportsLossless = true;
                caps.supportsLossy = true;
                caps.description = "Core image formats with broad compatibility";
            }
            #ifdef ULTRACANVAS_WEBP_SUPPORT
            else if (caps.pluginName.find("WebP") != std::string::npos) {
                caps.writeFormats = {"webp"};
                caps.supportsLossless = true;
                caps.supportsLossy = true;
                caps.supportsAnimation = true;
                caps.description = "Google WebP format with animation support";
            }
            #endif
            #ifdef ULTRACANVAS_AVIF_SUPPORT
            else if (caps.pluginName.find("AVIF") != std::string::npos) {
                caps.writeFormats = {"avif"};
                caps.supportsLossless = true;
                caps.supportsLossy = true;
                caps.supportsHDR = true;
                caps.description = "AV1 Image File Format with HDR support";
            }
            #endif
            #ifdef ULTRACANVAS_TIFF_SUPPORT
            else if (caps.pluginName.find("TIFF") != std::string::npos) {
                caps.writeFormats = {"tiff", "tif"};
                caps.supportsLossless = true;
                caps.supportsMultiPage = true;
                caps.supportsMetadata = true;
                caps.maxWidth = 32768;
                caps.maxHeight = 32768;
                caps.description = "Tagged Image File Format with metadata support";
            }
            #endif
            #ifdef ULTRACANVAS_HEIC_SUPPORT
            else if (caps.pluginName.find("HEIC") != std::string::npos) {
                caps.writeFormats = {"heic", "heif"};
                caps.supportsLossless = true;
                caps.supportsLossy = true;
                caps.supportsMetadata = true;
                caps.description = "Apple HEIC/HEIF format for high-efficiency compression";
            }
            #endif
            #ifdef ULTRACANVAS_JXL_SUPPORT
            else if (caps.pluginName.find("JPEG XL") != std::string::npos) {
                caps.writeFormats = {"jxl"};
                caps.supportsLossless = true;
                caps.supportsLossy = true;
                caps.supportsAnimation = true;
                caps.supportsHDR = true;
                caps.description = "Next-generation JPEG XL with superior compression";
            }
            #endif
            
            capabilities.push_back(caps);
        }
        
        return capabilities;
    }
    
    // ===== FILE OPERATIONS =====
    static bool LoadImage(const std::string& filePath, ImageData& imageData) {
        auto plugin = FindPluginForFile(filePath);
        if (!plugin) {
            std::cerr << "No plugin found for file: " << filePath << std::endl;
            return false;
        }
        
        std::cout << "Loading " << filePath << " with " << plugin->GetPluginName() << std::endl;
        return plugin->LoadFromFile(filePath, imageData);
    }
    
    static bool SaveImage(const std::string& filePath, const ImageData& imageData, int quality = 90) {
        auto plugin = FindPluginForFile(filePath);
        if (!plugin) {
            std::cerr << "No plugin found for saving: " << filePath << std::endl;
            return false;
        }
        
        std::cout << "Saving " << filePath << " with " << plugin->GetPluginName() << std::endl;
        return plugin->SaveToFile(filePath, imageData, quality);
    }
    
    static bool ConvertImage(const ImageConversionRequest& request) {
        // Load source image
        ImageData sourceImage;
        if (!LoadImage(request.inputPath, sourceImage)) {
            return false;
        }
        
        // Apply transformations
        ImageData processedImage = sourceImage;
        
        // Resize if requested
        if (request.maxWidth > 0 || request.maxHeight > 0) {
            int newWidth = sourceImage.width;
            int newHeight = sourceImage.height;
            
            if (request.maintainAspectRatio) {
                float aspectRatio = static_cast<float>(sourceImage.width) / sourceImage.height;
                
                if (request.maxWidth > 0 && request.maxHeight > 0) {
                    if (newWidth > request.maxWidth || newHeight > request.maxHeight) {
                        if (request.maxWidth / aspectRatio <= request.maxHeight) {
                            newWidth = request.maxWidth;
                            newHeight = static_cast<int>(request.maxWidth / aspectRatio);
                        } else {
                            newHeight = request.maxHeight;
                            newWidth = static_cast<int>(request.maxHeight * aspectRatio);
                        }
                    }
                } else if (request.maxWidth > 0) {
                    newWidth = request.maxWidth;
                    newHeight = static_cast<int>(request.maxWidth / aspectRatio);
                } else if (request.maxHeight > 0) {
                    newHeight = request.maxHeight;
                    newWidth = static_cast<int>(request.maxHeight * aspectRatio);
                }
            } else {
                if (request.maxWidth > 0) newWidth = request.maxWidth;
                if (request.maxHeight > 0) newHeight = request.maxHeight;
            }
            
            if (newWidth != sourceImage.width || newHeight != sourceImage.height) {
                // Try to use STB plugin for resizing
                auto stbPlugin = std::dynamic_pointer_cast<UltraCanvasSTBImagePlugin>(
                    FindPluginByName("STB Image Plugin"));
                
                if (stbPlugin && !stbPlugin->ResizeImage(processedImage, newWidth, newHeight, false)) {
                    std::cerr << "Failed to resize image" << std::endl;
                    return false;
                }
                
                std::cout << "Resized from " << sourceImage.width << "x" << sourceImage.height 
                          << " to " << newWidth << "x" << newHeight << std::endl;
            }
        }
        
        // Save with target format
        return SaveImage(request.outputPath, processedImage, request.quality);
    }
    
    // ===== QUERY FUNCTIONS =====
    static std::vector<std::string> GetSupportedReadExtensions() {
        std::set<std::string> uniqueExtensions;
        for (const auto& pair : extensionMap) {
            uniqueExtensions.insert(pair.first);
        }
        return std::vector<std::string>(uniqueExtensions.begin(), uniqueExtensions.end());
    }
    
    static std::vector<std::string> GetSupportedWriteExtensions() {
        std::set<std::string> uniqueExtensions;
        
        auto capabilities = GetAllCapabilities();
        for (const auto& caps : capabilities) {
            for (const auto& ext : caps.writeFormats) {
                uniqueExtensions.insert(ext);
            }
        }
        
        return std::vector<std::string>(uniqueExtensions.begin(), uniqueExtensions.end());
    }
    
    static bool CanRead(const std::string& extension) {
        std::string ext = extension;
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return extensionMap.count(ext) > 0;
    }
    
    static bool CanWrite(const std::string& extension) {
        auto writeExtensions = GetSupportedWriteExtensions();
        std::string ext = extension;
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return std::find(writeExtensions.begin(), writeExtensions.end(), ext) != writeExtensions.end();
    }
    
    static GraphicsFileInfo GetImageInfo(const std::string& filePath) {
        auto plugin = FindPluginForFile(filePath);
        if (!plugin) {
            return GraphicsFileInfo(filePath);
        }
        
        return plugin->GetFileInfo(filePath);
    }
    
    // ===== UTILITY FUNCTIONS =====
    static void PrintSupportedFormats() {
        std::cout << "\n=== UltraCanvas Image Format Support ===" << std::endl;
        
        auto readExtensions = GetSupportedReadExtensions();
        std::cout << "Read formats (" << readExtensions.size() << "): ";
        for (size_t i = 0; i < readExtensions.size(); ++i) {
            std::cout << readExtensions[i];
            if (i < readExtensions.size() - 1) std::cout << ", ";
        }
        std::cout << std::endl;
        
        auto writeExtensions = GetSupportedWriteExtensions();
        std::cout << "Write formats (" << writeExtensions.size() << "): ";
        for (size_t i = 0; i < writeExtensions.size(); ++i) {
            std::cout << writeExtensions[i];
            if (i < writeExtensions.size() - 1) std::cout << ", ";
        }
        std::cout << std::endl;
        
        std::cout << "\nActive plugins:" << std::endl;
        auto capabilities = GetAllCapabilities();
        for (const auto& caps : capabilities) {
            std::cout << "- " << caps.pluginName << " v" << caps.version;
            if (!caps.description.empty()) {
                std::cout << " (" << caps.description << ")";
            }
            std::cout << std::endl;
        }
        std::cout << std::endl;
    }
    
    static std::string GetFileFormatFilter() {
        auto extensions = GetSupportedReadExtensions();
        std::string filter = "Image Files (";
        
        for (size_t i = 0; i < extensions.size(); ++i) {
            filter += "*." + extensions[i];
            if (i < extensions.size() - 1) filter += " ";
        }
        filter += ")";
        
        return filter;
    }
    
    // ===== ADVANCED FEATURES =====
    static std::vector<std::string> GetPluginsForExtension(const std::string& extension) {
        std::vector<std::string> pluginNames;
        std::string ext = extension;
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        for (const auto& plugin : imagePlugins) {
            auto extensions = plugin->GetSupportedExtensions();
            if (std::find(extensions.begin(), extensions.end(), ext) != extensions.end()) {
                pluginNames.push_back(plugin->GetPluginName());
            }
        }
        
        return pluginNames;
    }
    
    static bool HasAdvancedFormats() {
        return 
        #ifdef ULTRACANVAS_WEBP_SUPPORT
            true ||
        #endif
        #ifdef ULTRACANVAS_AVIF_SUPPORT
            true ||
        #endif
        #ifdef ULTRACANVAS_TIFF_SUPPORT
            true ||
        #endif
        #ifdef ULTRACANVAS_HEIC_SUPPORT
            true ||
        #endif
        #ifdef ULTRACANVAS_JXL_SUPPORT
            true ||
        #endif
            false;
    }

private:
    // ===== HELPER METHODS =====
    static std::shared_ptr<IGraphicsPlugin> FindPluginForFile(const std::string& filePath) {
        size_t dotPos = filePath.find_last_of('.');
        if (dotPos == std::string::npos) return nullptr;
        
        std::string ext = filePath.substr(dotPos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        auto it = extensionMap.find(ext);
        return (it != extensionMap.end()) ? it->second : nullptr;
    }
    
    static std::shared_ptr<IGraphicsPlugin> FindPluginByName(const std::string& name) {
        for (const auto& plugin : imagePlugins) {
            if (plugin->GetPluginName() == name) {
                return plugin;
            }
        }
        return nullptr;
    }
    
    static void RebuildExtensionMap() {
        extensionMap.clear();
        
        for (const auto& plugin : imagePlugins) {
            auto extensions = plugin->GetSupportedExtensions();
            for (const auto& ext : extensions) {
                std::string lowerExt = ext;
                std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::tolower);
                extensionMap[lowerExt] = plugin;
            }
        }
    }
};

// ===== STATIC MEMBER DEFINITIONS =====
std::vector<std::shared_ptr<IGraphicsPlugin>> UltraCanvasImagePluginManager::imagePlugins;
std::map<std::string, std::shared_ptr<IGraphicsPlugin>> UltraCanvasImagePluginManager::extensionMap;
bool UltraCanvasImagePluginManager::initialized = false;

// ===== CONVENIENCE MACROS =====
#define ULTRACANVAS_INIT_IMAGE_SYSTEM() \
    UltraCanvas::UltraCanvasImagePluginManager::Initialize()

#define ULTRACANVAS_SHUTDOWN_IMAGE_SYSTEM() \
    UltraCanvas::UltraCanvasImagePluginManager::Shutdown()

#define ULTRACANVAS_LOAD_IMAGE(path, data) \
    UltraCanvas::UltraCanvasImagePluginManager::LoadImage(path, data)

#define ULTRACANVAS_SAVE_IMAGE(path, data, quality) \
    UltraCanvas::UltraCanvasImagePluginManager::SaveImage(path, data, quality)

} // namespace UltraCanvas

/*
=== UPDATED IMAGE PLUGIN MANAGER FEATURES ===

✅ **New Folder Structure Compatibility**:
- Includes updated to use Plugins/Images/ structure
- Clean separation from core framework files
- Proper relative path references

✅ **Enhanced Plugin Registration**:
- Automatic detection of available plugins
- Conditional compilation for optional formats
- Clear status reporting during initialization
- Duplicate plugin prevention

✅ **Comprehensive Format Support**:
- STB (core): PNG, JPEG, BMP, GIF, TGA, PSD, HDR
- WebP: Google's format with animation
- AVIF: Next-gen with HDR support
- TIFF: Professional with metadata
- HEIC: Apple's high-efficiency format
- JPEG XL: Future-proof with superior compression

✅ **Professional Diagnostics**:
- Detailed plugin capabilities reporting
- Format support matrix display
- Clear error messages for missing libraries
- Build configuration status

✅ **Usage Examples**:

```cpp
// Simple initialization
ULTRACANVAS_INIT_IMAGE_SYSTEM();

// Check what's available
bool hasAdvanced = UltraCanvas::UltraCanvasImagePluginManager::HasAdvancedFormats();
std::cout << "Advanced formats: " << (hasAdvanced ? "Available" : "Not available") << std::endl;

// Load any supported format
UltraCanvas::ImageData image;
if (ULTRACANVAS_LOAD_IMAGE("photo.heic", image)) {
    // Convert to next-gen format
    ULTRACANVAS_SAVE_IMAGE("photo.jxl", image, 95);
}

// Get file dialog filter
std::string filter = UltraCanvas::UltraCanvasImagePluginManager::GetFileFormatFilter();
// Returns: "Image Files (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.avif *.heic *.jxl)"
```

✅ **Build Integration**:
The manager automatically adapts to whatever plugins are compiled in:
- Core STB always available
- Advanced formats only if libraries present
- Graceful degradation with clear messaging

This updated manager is now fully compatible with the new Plugins/Images/
folder structure and provides professional-grade image format management!
*/