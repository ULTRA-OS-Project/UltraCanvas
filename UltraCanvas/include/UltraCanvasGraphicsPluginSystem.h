// Enhanced UltraCanvasGraphicsPluginSystem.h - Add to existing file
// Add these enhancements to the existing GraphicsPluginSystem

namespace UltraCanvas {

// ===== ENHANCED FORMAT TYPES =====
enum class GraphicsFormatType {
    Unknown,
    Bitmap,
    Vector,
    Animation,    // GIF, animated WebP, etc.
    ThreeD,       // 3D models
    Video,        // MP4, AVI, etc. (NEW)
    Text,         // PDF, HTML, etc. (NEW) 
    Data,         // CSV, ICS, etc. (NEW)
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
    GraphicsManipulation supportedManipulations = GraphicsManipulation::None;
    
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

// ===== ENHANCED PLUGIN REGISTRY METHODS =====
// Add these methods to UltraCanvasGraphicsPluginRegistry:

class UltraCanvasGraphicsPluginRegistry {
    // ... existing methods ...
    
public:
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
};

} // namespace UltraCanvas