// UltraCanvasSVGPlugin.h
// Complete SVG rendering plugin with gradient, filter, and transform support
// Version: 2.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasUI.h"
#include "tinyxml2.h"
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <stack>
#include <regex>
#include <sstream>

namespace UltraCanvas {

// Forward declarations
    class SVGDocument;
    class SVGElementRenderer;
    class IRenderContext;

// SVG Transform structure
    struct SVGTransform {
        enum Type {
            Matrix,
            Translate,
            Scale,
            Rotate,
            SkewX,
            SkewY
        };

        Type type;
        std::vector<float> values;

        void ApplyToContext(IRenderContext* ctx) const;
    };

// SVG Style properties
    struct SVGStyle {
        Color fillColor{0, 0, 0, 0};
        Color strokeColor{0, 0, 0, 0};
        float strokeWidth{1.0f};
        float opacity{1.0f};
        float fillOpacity{1.0f};
        float strokeOpacity{1.0f};
        std::string fillGradientId;
        std::string strokeGradientId;
        std::string filterId;
        std::string clipPathId;
        std::string maskId;
        enum FillRule { NonZero, EvenOdd } fillRule{NonZero};
        enum LineCap { Butt, Round, Square } lineCap{Butt};
        enum LineJoin { Miter, RoundJoin, Bevel } lineJoin{Miter};
        float miterLimit{4.0f};
        UCDashPattern dashPattern;

        void ParseFromAttributes(const tinyxml2::XMLElement* elem);
        void ParseFromStyle(const std::string& styleStr);
    };

// Base gradient class
    class SVGGradient {
    public:
        enum Type { Linear, Radial };

        Type type;
        std::vector<GradientStop> stops;
        std::string id;
        std::string href; // Reference to another gradient
        std::string units{"objectBoundingBox"}; // userSpaceOnUse or objectBoundingBox
        std::string spreadMethod{"pad"}; // pad, reflect, repeat
        SVGTransform transform;

        virtual ~SVGGradient() = default;
        virtual std::shared_ptr<IPaintPattern> CreatePattern(IRenderContext* ctx, const Rect2Df& bounds) = 0;
    };

// Linear gradient
    class SVGLinearGradient : public SVGGradient {
    public:
        float x1{0}, y1{0}, x2{1}, y2{0};

        std::shared_ptr<IPaintPattern> CreatePattern(IRenderContext* ctx, const Rect2Df& bounds) override;
    };

// Radial gradient
    class SVGRadialGradient : public SVGGradient {
    public:
        float cx{0.5f}, cy{0.5f}, r{0.5f};
        float fx{0.5f}, fy{0.5f}, fr{0};

        std::shared_ptr<IPaintPattern> CreatePattern(IRenderContext* ctx, const Rect2Df& bounds) override;
    };

// Filter effect base
    class SVGFilterEffect {
    public:
        std::string in;
        std::string result;
        virtual ~SVGFilterEffect() = default;
        virtual void Apply(IRenderContext* ctx) = 0;
    };

// Gaussian blur filter
    class SVGGaussianBlur : public SVGFilterEffect {
    public:
        float stdDeviationX{0}, stdDeviationY{0};
        void Apply(IRenderContext* ctx) override;
    };

// SVG Filter
    class SVGFilter {
    public:
        std::string id;
        std::vector<std::unique_ptr<SVGFilterEffect>> effects;
        Rect2Df filterRegion;
        std::string filterUnits{"objectBoundingBox"};

        void Apply(IRenderContext* ctx, const Rect2Df& bounds);
    };

// Path command for SVG path parsing
    struct PathCommand {
        char type;
        std::vector<float> params;
    };

// SVG Path parser
    class SVGPathParser {
    public:
        static std::vector<PathCommand> Parse(const std::string& pathData);
        static void RenderPath(IRenderContext* ctx, const std::vector<PathCommand>& commands);
        static std::vector<float> ParseNumbers(const std::string& str, size_t& pos);

    private:
        static void SkipWhitespace(const std::string& str, size_t& pos);
    };

// SVG Document class
    class SVGDocument {
    public:
        SVGDocument();
        ~SVGDocument();

        bool LoadFromFile(const std::string& filepath);
        bool LoadFromString(const std::string& svgContent);

        float GetWidth() const { return width; }
        float GetHeight() const { return height; }
        Rect2Df GetViewBox() const { return viewBox; }

        // Resource management
        void AddGradient(std::unique_ptr<SVGGradient> gradient);
        void AddFilter(std::unique_ptr<SVGFilter> filter);
        void AddClipPath(const std::string& id, std::vector<PathCommand> path);

        SVGGradient* GetGradient(const std::string& id);
        SVGFilter* GetFilter(const std::string& id);
        std::vector<PathCommand>* GetClipPath(const std::string& id);

        tinyxml2::XMLElement* GetRootElement() { return root; }

    private:
        tinyxml2::XMLDocument xmlDoc;
        tinyxml2::XMLElement* root{nullptr};

        float width{0}, height{0};
        Rect2Df viewBox;

        // Resource definitions
        std::unordered_map<std::string, std::unique_ptr<SVGGradient>> gradients;
        std::unordered_map<std::string, std::unique_ptr<SVGFilter>> filters;
        std::unordered_map<std::string, std::vector<PathCommand>> clipPaths;
        std::unordered_map<std::string, SVGStyle> styleSheets;

        void ParseDefs(tinyxml2::XMLElement* defs);
        void ParseGradient(tinyxml2::XMLElement* elem);
        void ParseFilter(tinyxml2::XMLElement* elem);
        void ParseClipPath(tinyxml2::XMLElement* elem);
        void ParseViewBox(const std::string& viewBoxStr);

        friend class SVGElementRenderer;
    };

// SVG Element Renderer
    class SVGElementRenderer {
    public:
        SVGElementRenderer(const SVGDocument& doc, IRenderContext* ctx);
        ~SVGElementRenderer();

        void Render(IRenderContext* ctx);
        void RenderElement(tinyxml2::XMLElement* elem);

    private:
        const SVGDocument& document;
        IRenderContext* context;

        // Rendering state stack
        std::stack<SVGStyle> styleStack;
//        std::stack<SVGTransform> transformStack;
//        std::stack<float> opacityStack;

        // Rendering methods for different elements
        void RenderGroup(tinyxml2::XMLElement* elem);
        void RenderPath(tinyxml2::XMLElement* elem);
        void RenderRect(tinyxml2::XMLElement* elem);
        void RenderCircle(tinyxml2::XMLElement* elem);
        void RenderEllipse(tinyxml2::XMLElement* elem);
        void RenderLine(tinyxml2::XMLElement* elem);
        void RenderPolyline(tinyxml2::XMLElement* elem);
        void RenderPolygon(tinyxml2::XMLElement* elem);
        void RenderText(tinyxml2::XMLElement* elem);
        void RenderImage(tinyxml2::XMLElement* elem);
        void RenderUse(tinyxml2::XMLElement* elem);

        // Helper methods
        void PushStyle(const SVGStyle& style);
        void PopStyle();
//        void PushTransform(const SVGTransform& transform);
//        void PopTransform();
//        void ApplyStyle(const SVGStyle& style);

        void ApplyFill(const SVGStyle& style, const Rect2Df & bounds);
        void ApplyStroke(const SVGStyle& style, const Rect2Df& bounds);
        void FillAndStroke(const SVGStyle& style, const Rect2Df& bounds);

        SVGStyle ParseStyle(tinyxml2::XMLElement* elem);
        SVGTransform ParseTransform(const std::string& transformStr);
        std::vector<Point2Df> ParsePoints(const std::string& pointsStr);
        Color ParseColor(const std::string& colorStr);
        float ParseLength(const std::string& lengthStr, float reference = 100.0f);

        Rect2Df GetElementBounds(tinyxml2::XMLElement* elem);
    };

// UltraCanvasSVGElement - UI element wrapper for SVG
    class UltraCanvasSVGElement : public UltraCanvasUIElement {
    public:
        UltraCanvasSVGElement(const std::string& identifier, long id, long x, long y, long w, long h);
        virtual ~UltraCanvasSVGElement() = default;

        bool LoadFromFile(const std::string& filepath);
        bool LoadFromString(const std::string& svgContent);

        void Render(IRenderContext* ctx) override;

        void SetScale(float scale) { this->scale = scale; }
        float GetScale() const { return scale; }

        void SetPreserveAspectRatio(bool preserve) { preserveAspectRatio = preserve; }
        bool GetPreserveAspectRatio() const { return preserveAspectRatio; }

        const SVGDocument* GetDocument() {
            return document.get();
        }
    private:
        std::unique_ptr<SVGDocument> document;
        float scale{1.0f};
        bool preserveAspectRatio{true};
    };

// SVG Plugin for UltraCanvas
//    class UltraCanvasSVGPlugin : public IUltraCanvasPlugin {
//    public:
//        UltraCanvasSVGPlugin();
//        virtual ~UltraCanvasSVGPlugin();
//
//        virtual bool Initialize() override;
//        virtual void Shutdown() override;
//        virtual std::string GetName() const override { return "SVGPlugin"; }
//        virtual std::string GetVersion() const override { return "2.0.0"; }
//
//        // Factory method
//        static std::unique_ptr<UltraCanvasSVGElement> CreateSVGElement(const std::string& filepath);
//
//    private:
//        static UltraCanvasSVGPlugin* instance;
//    };

} // namespace UltraCanvas