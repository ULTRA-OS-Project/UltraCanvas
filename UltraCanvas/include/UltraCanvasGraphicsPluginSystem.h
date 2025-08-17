// UltraCanvasGraphicsPluginSystem.h
// Complete graphics plugin system with all required components
// Version: 1.2.4
// Last Modified: 2025-08-17
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasImageElement.h"
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <set>

namespace UltraCanvas {

// Forward declarations
    class UltraCanvasElement;
    struct GraphicsFileInfo;

// ===== GRAPHICS MANIPULATION ENUM =====

    enum class GraphicsManipulation : uint32_t {
        NoManupulation = 0,
        Move = 1 << 0,           // Can be moved/translated
        Rotate = 1 << 1,         // Can be rotated
        Scale = 1 << 2,          // Can be scaled/resized
        Resize = 1 << 3,         // Can be resized (different from scale)
        Flip = 1 << 4,           // Can be flipped horizontally/vertically
        Crop = 1 << 5,           // Can be cropped
        Transform = 1 << 6,      // Can apply matrix transformations
        Compress = 1 << 7,       // Can adjust compression/quality
        ColorAdjust = 1 << 8,    // Can adjust colors/brightness/contrast
        Filter = 1 << 9,         // Can apply filters/effects

        // Combinations
        Basic = Move | Scale,
        Standard = Move | Rotate | Scale | Resize,
        Advanced = Standard | Flip | Crop | Transform,
        All = 0xFFFFFFFF
    };

// Enable bitwise operations for GraphicsManipulation
    inline GraphicsManipulation operator|(GraphicsManipulation a, GraphicsManipulation b) {
        return static_cast<GraphicsManipulation>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline GraphicsManipulation operator&(GraphicsManipulation a, GraphicsManipulation b) {
        return static_cast<GraphicsManipulation>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    inline bool HasManipulation(GraphicsManipulation flags, GraphicsManipulation flag) {
        return (flags & flag) == flag;
    }

// ===== ENHANCED FORMAT TYPES =====
    enum class GraphicsFormatType {
        Unknown,
        Bitmap,
        Vector,
        Animation,    // GIF, animated WebP, etc.
        ThreeD,       // 3D models
        Video,        // MP4, AVI, etc.
        Text,         // PDF, HTML, etc.
        Data,         // CSV, ICS, etc.
        Procedural    // Generated graphics
    };

// ===== COMPREHENSIVE FORMAT DETECTION =====
    class GraphicsFormatDetector {
    public:
        static GraphicsFormatType DetectFromExtension(const std::string& extension) {
            std::string ext = extension;
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            // Remove leading dot if present
            if (!ext.empty() && ext[0] == '.') {
                ext = ext.substr(1);
            }

            static const std::unordered_map<std::string, GraphicsFormatType> formatMap = {
                    // Bitmap formats
                    {"heic", GraphicsFormatType::Bitmap}, {"heif", GraphicsFormatType::Bitmap},
                    {"avif", GraphicsFormatType::Bitmap}, {"webp", GraphicsFormatType::Bitmap},
                    {"jpg", GraphicsFormatType::Bitmap}, {"jpeg", GraphicsFormatType::Bitmap},
                    {"png", GraphicsFormatType::Bitmap}, {"bmp", GraphicsFormatType::Bitmap},
                    {"tiff", GraphicsFormatType::Bitmap}, {"tif", GraphicsFormatType::Bitmap},
                    {"psp", GraphicsFormatType::Bitmap}, {"ico", GraphicsFormatType::Bitmap},
                    {"cur", GraphicsFormatType::Bitmap}, {"hdr", GraphicsFormatType::Bitmap},
                    {"raw", GraphicsFormatType::Bitmap}, {"jfif", GraphicsFormatType::Bitmap},

                    // Animated bitmap formats
                    {"gif", GraphicsFormatType::Animation},

                    // Vector formats
                    {"svg", GraphicsFormatType::Vector}, {"xar", GraphicsFormatType::Vector},
                    {"ger", GraphicsFormatType::Vector}, {"ai", GraphicsFormatType::Vector},
                    {"eps", GraphicsFormatType::Vector}, {"ps", GraphicsFormatType::Vector},

                    // 3D model formats
                    {"3dm", GraphicsFormatType::ThreeD}, {"3ds", GraphicsFormatType::ThreeD},
                    {"pov", GraphicsFormatType::ThreeD}, {"std", GraphicsFormatType::ThreeD},
                    {"obj", GraphicsFormatType::ThreeD}, {"fbx", GraphicsFormatType::ThreeD},
                    {"dae", GraphicsFormatType::ThreeD}, {"gltf", GraphicsFormatType::ThreeD},

                    // Video formats
                    {"mp4", GraphicsFormatType::Video}, {"mpg", GraphicsFormatType::Video},
                    {"mpeg", GraphicsFormatType::Video}, {"avi", GraphicsFormatType::Video},
                    {"mov", GraphicsFormatType::Video}, {"wmv", GraphicsFormatType::Video},
                    {"flv", GraphicsFormatType::Video}, {"mkv", GraphicsFormatType::Video},
                    {"heiv", GraphicsFormatType::Video},

                    // Text/Document formats
                    {"pdf", GraphicsFormatType::Text}, {"html", GraphicsFormatType::Text},
                    {"htm", GraphicsFormatType::Text}, {"txt", GraphicsFormatType::Text},
                    {"rtf", GraphicsFormatType::Text}, {"doc", GraphicsFormatType::Text},
                    {"docx", GraphicsFormatType::Text}, {"odt", GraphicsFormatType::Text},
                    {"eml", GraphicsFormatType::Text}, {"ods", GraphicsFormatType::Text},

                    // Data formats
                    {"csv", GraphicsFormatType::Data}, {"json", GraphicsFormatType::Data},
                    {"xml", GraphicsFormatType::Data}, {"ics", GraphicsFormatType::Data},
                    {"prt", GraphicsFormatType::Data}, {"dat", GraphicsFormatType::Data}
            };

            auto it = formatMap.find(ext);
            return (it != formatMap.end()) ? it->second : GraphicsFormatType::Unknown;
        }

        static std::vector<std::string> GetExtensionsForType(GraphicsFormatType type) {
            std::vector<std::string> extensions;

            switch (type) {
                case GraphicsFormatType::Bitmap:
                    return {"png", "jpg", "jpeg", "bmp", "tiff", "webp", "avif", "heic", "ico", "raw"};
                case GraphicsFormatType::Vector:
                    return {"svg", "ai", "eps", "ps"};
                case GraphicsFormatType::Animation:
                    return {"gif"};
                case GraphicsFormatType::ThreeD:
                    return {"3ds", "3dm", "obj", "fbx", "dae", "gltf"};
                case GraphicsFormatType::Video:
                    return {"mp4", "avi", "mov", "wmv", "mkv", "mpg", "mpeg"};
                case GraphicsFormatType::Text:
                    return {"pdf", "html", "txt", "doc", "docx", "rtf"};
                case GraphicsFormatType::Data:
                    return {"csv", "json", "xml", "ics"};
                default:
                    return {};
            }
        }

        static bool IsImageFormat(GraphicsFormatType type) {
            return type == GraphicsFormatType::Bitmap ||
                   type == GraphicsFormatType::Vector ||
                   type == GraphicsFormatType::Animation;
        }

        static bool IsMediaFormat(GraphicsFormatType type) {
            return IsImageFormat(type) ||
                   type == GraphicsFormatType::Video ||
                   type == GraphicsFormatType::ThreeD;
        }
    };

// ===== ENHANCED GraphicsFileInfo =====
    struct GraphicsFileInfo {
        std::string filename;
        std::string extension;
        GraphicsFormatType formatType = GraphicsFormatType::Unknown;
        GraphicsManipulation supportedManipulations = GraphicsManipulation::NoManupulation;

        // File properties
        size_t fileSize = 0;
        int width = 0;
        int height = 0;
        int depth = 0;
        int channels = 0;
        int bitDepth = 8;
        bool hasAlpha = false;
        bool isAnimated = false;
        int frameCount = 1;

        // Enhanced properties
        std::string mimeType;
        std::string colorSpace;
        float duration = 0.0f;  // For video/animation

        // Metadata
        std::map<std::string, std::string> metadata;

        GraphicsFileInfo() = default;
        GraphicsFileInfo(const std::string& path) : filename(path) {
            UpdateFromPath(path);
        }

        void UpdateFromPath(const std::string& path) {
            filename = path;
            size_t dotPos = path.find_last_of('.');
            if (dotPos != std::string::npos) {
                extension = path.substr(dotPos + 1);
                std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

                // Use enhanced format detection
                formatType = GraphicsFormatDetector::DetectFromExtension(extension);

                // Set MIME type
                mimeType = GetMimeType();
            }
        }

        std::string GetMimeType() const {
            static const std::unordered_map<std::string, std::string> mimeMap = {
                    {"png", "image/png"}, {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"},
                    {"gif", "image/gif"}, {"bmp", "image/bmp"}, {"webp", "image/webp"},
                    {"svg", "image/svg+xml"}, {"pdf", "application/pdf"},
                    {"mp4", "video/mp4"}, {"avi", "video/x-msvideo"},
                    {"csv", "text/csv"}, {"json", "application/json"},
                    {"html", "text/html"}, {"txt", "text/plain"}
            };

            auto it = mimeMap.find(extension);
            return (it != mimeMap.end()) ? it->second : "application/octet-stream";
        }

        bool IsValid() const {
            return !filename.empty() && !extension.empty() && formatType != GraphicsFormatType::Unknown;
        }

        bool CanDisplay() const {
            return GraphicsFormatDetector::IsImageFormat(formatType);
        }

        bool RequiresPlugin() const {
            return formatType == GraphicsFormatType::Video ||
                   formatType == GraphicsFormatType::ThreeD ||
                   formatType == GraphicsFormatType::Text ||
                   formatType == GraphicsFormatType::Data;
        }
    };

// ===== GRAPHICS PLUGIN INTERFACE =====
    class IGraphicsPlugin {
    public:
        virtual ~IGraphicsPlugin() = default;

        // Basic plugin information
        virtual std::string GetPluginName() const = 0;
        virtual std::string GetPluginVersion() const = 0;
        virtual std::vector<std::string> GetSupportedExtensions() const = 0;

        // File handling
        virtual bool CanHandle(const std::string& filePath) const = 0;
        virtual bool CanHandle(const GraphicsFileInfo& fileInfo) const = 0;

        // Graphics operations
        virtual std::shared_ptr<UltraCanvasElement> LoadGraphics(const std::string& filePath) = 0;
        virtual std::shared_ptr<UltraCanvasElement> LoadGraphics(const GraphicsFileInfo& fileInfo) = 0;
        virtual std::shared_ptr<UltraCanvasElement> CreateGraphics(int width, int height, GraphicsFormatType type) = 0;

        // Information and capabilities
        virtual GraphicsManipulation GetSupportedManipulations() const = 0;
        virtual GraphicsFileInfo GetFileInfo(const std::string& filePath) = 0;
        virtual bool ValidateFile(const std::string& filePath) = 0;
    };

// ===== GRAPHICS PLUGIN REGISTRY =====
    class UltraCanvasGraphicsPluginRegistry {
    private:
        static std::vector<std::shared_ptr<IGraphicsPlugin>> plugins;
        static std::map<std::string, std::shared_ptr<IGraphicsPlugin>> extensionMap;
        static bool initialized;

        // Helper method - now properly declared
        static std::shared_ptr<IGraphicsPlugin> FindPluginForFile(const std::string& filePath) {
            size_t dotPos = filePath.find_last_of('.');
            if (dotPos == std::string::npos) return nullptr;

            std::string ext = filePath.substr(dotPos + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            auto it = extensionMap.find(ext);
            return (it != extensionMap.end()) ? it->second : nullptr;
        }

    public:
        // ===== INITIALIZATION =====
        static bool Initialize() {
            if (initialized) return true;

            plugins.clear();
            extensionMap.clear();

            initialized = true;
            return true;
        }

        static void Shutdown() {
            plugins.clear();
            extensionMap.clear();
            initialized = false;
        }

        // ===== PLUGIN MANAGEMENT =====
        static void RegisterPlugin(std::shared_ptr<IGraphicsPlugin> plugin) {
            if (!plugin) return;

            // Check for duplicates
            for (const auto& existing : plugins) {
                if (existing->GetPluginName() == plugin->GetPluginName()) {
                    return; // Already registered
                }
            }

            plugins.push_back(plugin);

            // Update extension mapping
            auto extensions = plugin->GetSupportedExtensions();
            for (const auto& ext : extensions) {
                std::string lowerExt = ext;
                std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::tolower);
                extensionMap[lowerExt] = plugin;
            }
        }

        static void UnregisterPlugin(const std::string& pluginName) {
            auto it = std::find_if(plugins.begin(), plugins.end(),
                                   [&pluginName](const std::shared_ptr<IGraphicsPlugin>& plugin) {
                                       return plugin->GetPluginName() == pluginName;
                                   });

            if (it != plugins.end()) {
                // Remove from extension map
                auto extensions = (*it)->GetSupportedExtensions();
                for (const auto& ext : extensions) {
                    std::string lowerExt = ext;
                    std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::tolower);
                    extensionMap.erase(lowerExt);
                }

                plugins.erase(it);
            }
        }

        // ===== QUERY METHODS =====
        static std::vector<std::string> GetSupportedExtensions() {
            std::vector<std::string> extensions;

            // Get extensions from all format types
            for (int i = 0; i <= static_cast<int>(GraphicsFormatType::Data); ++i) {
                auto typeExtensions = GraphicsFormatDetector::GetExtensionsForType(
                        static_cast<GraphicsFormatType>(i));
                extensions.insert(extensions.end(), typeExtensions.begin(), typeExtensions.end());
            }

            // Add plugin-specific extensions
            for (const auto& pair : extensionMap) {
                if (std::find(extensions.begin(), extensions.end(), pair.first) == extensions.end()) {
                    extensions.push_back(pair.first);
                }
            }

            return extensions;
        }

        static std::vector<std::string> GetSupportedExtensionsForType(GraphicsFormatType type) {
            return GraphicsFormatDetector::GetExtensionsForType(type);
        }

        static GraphicsFileInfo GetFileInfo(const std::string& filePath) {
            GraphicsFileInfo info(filePath);

            auto plugin = FindPluginForFile(filePath);
            if (plugin) {
                // Let plugin enhance the file info
                GraphicsFileInfo pluginInfo = plugin->GetFileInfo(filePath);

                // Merge information
                info.supportedManipulations = pluginInfo.supportedManipulations;
                info.width = pluginInfo.width;
                info.height = pluginInfo.height;
                info.fileSize = pluginInfo.fileSize;
                info.metadata = pluginInfo.metadata;
            }

            return info;
        }

        static bool CanHandle(const std::string& filePath) {
            GraphicsFileInfo info(filePath);
            return info.IsValid() && (info.CanDisplay() || FindPluginForFile(filePath) != nullptr);
        }

        static std::shared_ptr<UltraCanvasElement> LoadGraphics(const std::string& filePath) {
            auto plugin = FindPluginForFile(filePath);
            if (!plugin) return nullptr;

            return plugin->LoadGraphics(filePath);
        }

        static std::shared_ptr<UltraCanvasElement> CreateGraphics(int width, int height, GraphicsFormatType type) {
            for (const auto& plugin : plugins) {
                auto element = plugin->CreateGraphics(width, height, type);
                if (element) return element;
            }
            return nullptr;
        }

        // ===== UTILITY METHODS =====
        static std::vector<std::shared_ptr<IGraphicsPlugin>> GetAllPlugins() {
            return plugins;
        }

        static std::shared_ptr<IGraphicsPlugin> GetPluginByName(const std::string& name) {
            for (const auto& plugin : plugins) {
                if (plugin->GetPluginName() == name) {
                    return plugin;
                }
            }
            return nullptr;
        }

        static void PrintRegisteredPlugins() {
            std::cout << "Registered Graphics Plugins (" << plugins.size() << "):" << std::endl;
            for (const auto& plugin : plugins) {
                std::cout << "- " << plugin->GetPluginName()
                          << " v" << plugin->GetPluginVersion() << std::endl;

                auto extensions = plugin->GetSupportedExtensions();
                std::cout << "  Extensions: ";
                for (size_t i = 0; i < extensions.size(); ++i) {
                    std::cout << extensions[i];
                    if (i < extensions.size() - 1) std::cout << ", ";
                }
                std::cout << std::endl;
            }
        }
    };

// ===== STATIC MEMBER DEFINITIONS =====
    std::vector<std::shared_ptr<IGraphicsPlugin>> UltraCanvasGraphicsPluginRegistry::plugins;
    std::map<std::string, std::shared_ptr<IGraphicsPlugin>> UltraCanvasGraphicsPluginRegistry::extensionMap;
    bool UltraCanvasGraphicsPluginRegistry::initialized = false;

// ===== CONVENIENCE FUNCTIONS =====
    inline void InitializeGraphicsPluginSystem() {
        UltraCanvasGraphicsPluginRegistry::Initialize();
    }

    inline void ShutdownGraphicsPluginSystem() {
        UltraCanvasGraphicsPluginRegistry::Shutdown();
    }

    inline void RegisterGraphicsPlugin(std::shared_ptr<IGraphicsPlugin> plugin) {
        UltraCanvasGraphicsPluginRegistry::RegisterPlugin(plugin);
    }

    inline std::shared_ptr<UltraCanvasElement> LoadGraphicsFile(const std::string& filePath) {
        return UltraCanvasGraphicsPluginRegistry::LoadGraphics(filePath);
    }

    inline GraphicsFileInfo GetGraphicsFileInfo(const std::string& filePath) {
        return UltraCanvasGraphicsPluginRegistry::GetFileInfo(filePath);
    }

    inline bool CanHandleGraphicsFile(const std::string& filePath) {
        return UltraCanvasGraphicsPluginRegistry::CanHandle(filePath);
    }

} // namespace UltraCanvas