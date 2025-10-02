// Plugins/SVG/UltraCanvasSVGPlugin.h
// Lightweight SVG plugin that leverages existing UltraCanvas drawing infrastructure
// Version: 1.0.0
// Last Modified: 2025-07-17
// Author: UltraCanvas Framework
#pragma once

//#include "UltraCanvasGraphicsPluginSystem.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasCommonTypes.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <sstream>

namespace UltraCanvas {

// ===== SIMPLE SVG DOM STRUCTURES =====

struct SVGAttributes {
    std::unordered_map<std::string, std::string> attrs;
    
    std::string Get(const std::string& name, const std::string& defaultValue = "") const {
        auto it = attrs.find(name);
        return (it != attrs.end()) ? it->second : defaultValue;
    }
    
    float GetFloat(const std::string& name, float defaultValue = 0.0f) const {
        std::string value = Get(name);
        return value.empty() ? defaultValue : std::stof(value);
    }
    
    Color GetColor(const std::string& name, const Color& defaultValue = Colors::Black) const;
};

struct SVGElement {
    std::string tagName;
    SVGAttributes attributes;
    std::string textContent;
    std::vector<std::shared_ptr<SVGElement>> children;
    
    SVGElement(const std::string& tag) : tagName(tag) {}
};

struct SVGDocument {
    std::shared_ptr<SVGElement> root;
    float width = 100.0f;
    float height = 100.0f;
    Rect2Df viewBox;
    bool hasViewBox = false;
};

// ===== LIGHTWEIGHT SVG PARSER =====
class SimpleSVGParser {
private:
    std::string content;
    size_t position = 0;
    
    void SkipWhitespace();
    std::string ReadUntil(char delimiter);
    std::string ReadTagName();
    SVGAttributes ParseAttributes();
    std::shared_ptr<SVGElement> ParseElement();
    
public:
    std::shared_ptr<SVGDocument> Parse(const std::string& svgContent);
    
    // Utility color parsing
    static Color ParseColor(const std::string& colorStr);
    static float ParseLength(const std::string& lengthStr, float referenceValue = 100.0f);
    static std::vector<Point2Df> ParsePoints(const std::string& pointsStr);
    static std::vector<Point2Df> ParsePath(const std::string& pathStr);
};

// ===== SVG ELEMENT RENDERER =====
class SVGElementRenderer {
private:
    const SVGDocument& document;
    IRenderContext* ctx;
    
    void ApplyStyles(const SVGElement& element);
    void RenderRect(const SVGElement& element);
    void RenderCircle(const SVGElement& element);
    void RenderEllipse(const SVGElement& element);
    void RenderLine(const SVGElement& element);
    void RenderPolyline(const SVGElement& element);
    void RenderPolygon(const SVGElement& element);
    void RenderPath(const SVGElement& element);
    void RenderText(const SVGElement& element);
    void RenderGroup(const SVGElement& element);
    
public:
    SVGElementRenderer(const SVGDocument& doc, IRenderContext* rctx) : document(doc), ctx(rctx) {}
    
    void RenderElement(const SVGElement& element);
    void RenderDocument(const Rect2Df& viewport);
};

// ===== SVG UI ELEMENT =====
class UltraCanvasSVGElement : public UltraCanvasUIElement {
private:
    std::shared_ptr<SVGDocument> document;
    SVGElementRenderer* renderer = nullptr;
    std::string svgContent;
    bool autoResize = true;
    //float scaleFactor = 1.0f;
    
public:
    UltraCanvasSVGElement(const std::string& identifier, long id, long x, long y, long w, long h);
    virtual ~UltraCanvasSVGElement();
    
    // SVG loading
    bool LoadFromString(const std::string& svgContent);
    bool LoadFromFile(const std::string& filePath);
    
    // Properties
    void SetAutoResize(bool enable) { autoResize = enable; }
    bool GetAutoResize() const { return autoResize; }
    
//    void SetScaleFactor(float factor) { scaleFactor = factor; }
//    float GetScaleFactor() const { return scaleFactor; }
    
    std::shared_ptr<SVGDocument> GetDocument() const { return document; }
    const std::string& GetSVGContent() const { return svgContent; }
    
    // UltraCanvasUIElement interface
    void Render() override;
    bool OnEvent(const UCEvent& event) override { return false; }
    
    // Callbacks
    std::function<void(const std::string&)> onLoadError;
    std::function<void()> onLoadComplete;
    
private:
    void UpdateSizeFromSVG();
};

// ===== SVG GRAPHICS PLUGIN =====
//class UltraCanvasSVGPlugin : public IGraphicsPlugin {
//private:
//    std::unordered_map<std::string, std::shared_ptr<SVGDocument>> documentCache;
//    SimpleSVGParser parser;
//
//public:
//    // IGraphicsPlugin interface
//    std::string GetPluginName() const override {
//        return "UltraCanvas SVG Plugin";
//    }
//
//    std::string GetPluginVersion() const override {
//        return "1.0.0";
//    }
//
//    std::string GetDescription() const override {
//        return "Scalable Vector Graphics support using native UltraCanvas rendering";
//    }
//
//    std::vector<std::string> GetSupportedExtensions() const override {
//        return {".svg", ".svgz"};
//    }
//
//    GraphicsFormatType GetFormatType() const override {
//        return GraphicsFormatType::Vector;
//    }
//
//    bool CanHandle(const std::string& filePath) const override;
//    bool CanHandle(const GraphicsFileInfo& fileInfo) const override;
//
//    bool LoadFromFile(const std::string& filePath) override;
//    bool LoadFromMemory(const std::vector<uint8_t>& data) override;
//
//    void Render(const Rect2Di& bounds) override;
//
//    Size2D GetNaturalSize() const override;
//    GraphicsFileInfo GetFileInfo(const std::string& filePath) override;
//
//    // Plugin-specific methods
//    std::shared_ptr<SVGDocument> GetDocument(const std::string& key) const;
//    void ClearCache();
//
//private:
//    std::string currentKey;
//
//    std::string GetFileExtension(const std::string& filePath) const;
//    std::string GenerateKey(const std::string& identifier) const;
//};
//
//// ===== FACTORY FUNCTIONS =====
//inline std::shared_ptr<UltraCanvasSVGElement> CreateSVGElement(
//    const std::string& identifier, long id, long x, long y, long w, long h) {
//    return std::make_shared<UltraCanvasSVGElement>(identifier, id, x, y, w, h);
//}
//
//inline std::shared_ptr<UltraCanvasSVGElement> CreateSVGFromString(
//    const std::string& identifier, long id, long x, long y, long w, long h,
//    const std::string& svgContent) {
//    auto element = CreateSVGElement(identifier, id, x, y, w, h);
//    element->LoadFromString(svgContent);
//    return element;
//}
//
//inline std::shared_ptr<UltraCanvasSVGElement> CreateSVGFromFile(
//    const std::string& identifier, long id, long x, long y, long w, long h,
//    const std::string& filePath) {
//    auto element = CreateSVGElement(identifier, id, x, y, w, h);
//    element->LoadFromFile(filePath);
//    return element;
//}
//
//// ===== PLUGIN REGISTRATION =====
//inline void RegisterSVGPlugin() {
//    auto svgPlugin = std::make_shared<UltraCanvasSVGPlugin>();
//    UltraCanvasGraphicsPluginRegistry::RegisterPlugin(svgPlugin);
//}
//
//// ===== CONVENIENCE MACROS =====
//#define ULTRACANVAS_SVG_ELEMENT(name, id, x, y, w, h) \
//    UltraCanvas::CreateSVGElement(name, id, x, y, w, h)
//
//#define ULTRACANVAS_SVG_FROM_STRING(name, id, x, y, w, h, content) \
//    UltraCanvas::CreateSVGFromString(name, id, x, y, w, h, content)
//
//#define ULTRACANVAS_SVG_FROM_FILE(name, id, x, y, w, h, path) \
//    UltraCanvas::CreateSVGFromFile(name, id, x, y, w, h, path)

} // namespace UltraCanvas