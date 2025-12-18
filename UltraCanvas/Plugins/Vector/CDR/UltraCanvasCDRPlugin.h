// Plugins/Vector/CDR/UltraCanvasCDRPlugin.h
// CorelDRAW CDR/CMX file format plugin using libcdr and librevenge
// Version: 1.1.0
// Last Modified: 2025-12-15
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_CDR_PLUGIN_H
#define ULTRACANVAS_CDR_PLUGIN_H

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasGraphicsPluginSystem.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasUIElement.h"

#include <memory>
#include <string>
#include <vector>
#include <stack>
#include <map>
#include <functional>

namespace UltraCanvas {
// ===== ULTRACANVAS CDR ELEMENT =====
    enum class CDRFitMode { FitNone, FitWidth, FitHeight, FitPage };

// ===== CDR DOCUMENT PAGE =====
    struct CDRPage {
        float width = 0;
        float height = 0;
        std::vector<std::function<void(IRenderContext*)>> drawCommands;
    };

// ===== CDR DOCUMENT =====
    class CDRDocument {
    public:
        std::vector<CDRPage> pages;
        std::map<std::string, std::vector<GradientStop>> gradients;
        std::map<std::string, std::vector<uint8_t>> images;

        float documentWidth = 0;
        float documentHeight = 0;
        std::string title;
        std::string author;

        bool IsValid() const { return !pages.empty(); }
        int GetPageCount() const { return static_cast<int>(pages.size()); }
    };

// ===== CDR PARAGRAPH STYLE =====
    struct CDRParagraphStyle {
        std::string name;

        // Text alignment
        enum class Alignment { Left, Center, Right, Justify };
        Alignment textAlign = Alignment::Left;

        // Spacing
        float lineHeight = 1.2f;
        float marginTop = 0.0f;
        float marginBottom = 0.0f;
        float marginLeft = 0.0f;
        float marginRight = 0.0f;
        float textIndent = 0.0f;

        // Background
        Color backgroundColor = Colors::Transparent;
        bool hasBackground = false;
    };

// ===== CDR CHARACTER STYLE =====
    struct CDRCharacterStyle {
        std::string name;

        // Font properties
        std::string fontFamily = "Sans";
        float fontSize = 12.0f;
        FontWeight fontWeight = FontWeight::Normal;
        FontSlant fontSlant = FontSlant::Normal;

        // Color
        Color textColor = Colors::Black;

        // Decorations
        bool underline = false;
        bool strikethrough = false;
        bool overline = false;

        // Spacing
        float letterSpacing = 0.0f;

        // Transform
        enum class TextTransform { TransformNone, Uppercase, Lowercase, Capitalize };
        TextTransform textTransform = TextTransform::TransformNone;
    };

// ===== CDR STYLE STATE =====
    struct CDRStyleState {
        Color fillColor = Colors::Black;
        Color strokeColor = Colors::Transparent;
        float strokeWidth = 1.0f;
        float opacity = 1.0f;
        float fillOpacity = 1.0f;
        float strokeOpacity = 1.0f;

        LineCap lineCap = LineCap::Butt;
        LineJoin lineJoin = LineJoin::Miter;
        float miterLimit = 4.0f;
        UCDashPattern dashPattern;

        bool hasFill = true;
        bool hasStroke = false;

        std::string fillGradientId;
        std::string strokeGradientId;

        // Font properties
        std::string fontFamily = "Sans";
        float fontSize = 12.0f;
        FontWeight fontWeight = FontWeight::Normal;
        FontSlant fontSlant = FontSlant::Normal;

        // Named style references
        std::string paragraphStyleName;
        std::string characterStyleName;
    };


    class UltraCanvasCDRRenderer {
    public:

        bool LoadFromFile(const std::string& filePath);
        bool LoadFromMemory(const std::vector<uint8_t>& data);

        void SetFitMode(CDRFitMode mode);
        CDRFitMode GetFitMode() const { return fitMode; }
        bool IsLoaded() const { return document && document->IsValid(); }
        int GetPageCount() const { return document ? document->GetPageCount() : 0; }
        void SetViewport(float w, float h);
        void SetZoom(float zoom);
        float GetZoom() const { return zoomLevel; }
        void SetOffset(float x, float y);
        Point2Df GetOffset() const { return offset; }

        void RenderPage(IRenderContext* ctx, int pageIndex);
    private:
        std::shared_ptr<CDRDocument> document;
        float viewWidth = 0;
        float viewHeight = 0;
        float zoomLevel = 1.0f;
        Point2Df offset = {0, 0};
        CDRFitMode fitMode = CDRFitMode::FitPage;

        void CalculateAndSetFitTransform(IRenderContext* ctx, const CDRPage& page);
    };

// UI element that displays CDR content
    class UltraCanvasCDRElement : public UltraCanvasUIElement {
    private:
        UltraCanvasCDRRenderer cdrRenderer;
        int currentPageIndex = 0;
        void RenderPlaceholder(IRenderContext* ctx, const std::string& message);

    public:
        UltraCanvasCDRElement(const std::string& identifier, long id,
                              int x, int y, int width, int height);
        ~UltraCanvasCDRElement() override = default;

        // Load CDR file
        bool LoadFromFile(const std::string& filePath);
        bool LoadFromMemory(const std::vector<uint8_t>& data);

        // Document access
        bool IsLoaded() const { return cdrRenderer.IsLoaded(); }
        int GetPageCount() const { return cdrRenderer.GetPageCount(); }
        int GetCurrentPage() const { return currentPageIndex; }
        void SetCurrentPage(int page);
        void SetZoom(float zoom);
        float GetZoom() const { return cdrRenderer.GetZoom(); }
        void SetOffset(float x, float y);
        Point2Df GetOffset() const { return cdrRenderer.GetOffset(); }
        void SetFitMode(CDRFitMode mode);
        CDRFitMode GetFitMode() const { return cdrRenderer.GetFitMode(); }

        // Rendering
        void Render(IRenderContext* ctx) override;

        // Callbacks
        std::function<void(int)> onPageChanged;
        std::function<void(const std::string&)> onLoadError;
        std::function<void()> onLoadComplete;
    };

// ===== CDR PLUGIN =====
    class UltraCanvasCDRPlugin : public IGraphicsPlugin {
    public:
        UltraCanvasCDRPlugin() = default;
        ~UltraCanvasCDRPlugin() override = default;

        // IGraphicsPlugin interface
        std::string GetPluginName() const override { return "UltraCanvas CDR Plugin"; }
        std::string GetPluginVersion() const override { return "1.1.0"; }
        std::vector<std::string> GetSupportedExtensions() const override {
            return {"cdr", "cmx", "ccx", "cdt"};
        }

        bool CanHandle(const std::string& filePath) const override;
        bool CanHandle(const GraphicsFileInfo& fileInfo) const override;

        std::shared_ptr<UltraCanvasUIElement> LoadGraphics(const std::string& filePath) override;
        std::shared_ptr<UltraCanvasUIElement> LoadGraphics(const GraphicsFileInfo& fileInfo) override;
        std::shared_ptr<UltraCanvasUIElement> CreateGraphics(int width, int height,
                                                             GraphicsFormatType type) override;

        GraphicsManipulation GetSupportedManipulations() const override;
        GraphicsFileInfo GetFileInfo(const std::string& filePath) override;
        bool ValidateFile(const std::string& filePath) override;

        // Static utilities
        static bool IsFileSupported(const std::string& filePath);
        static std::shared_ptr<CDRDocument> ParseCDRFile(const std::string& filePath);
        static std::shared_ptr<CDRDocument> ParseCDRMemory(const std::vector<uint8_t>& data);
    };

// ===== CONVENIENCE FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasCDRElement> CreateCDRElement(
            const std::string& identifier, long id,
            int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasCDRElement>(identifier, id, x, y, width, height);
    }

    inline std::shared_ptr<UltraCanvasCDRElement> LoadCDRFromFile(
            const std::string& identifier, long id,
            int x, int y, int width, int height,
            const std::string& filePath) {
        auto element = CreateCDRElement(identifier, id, x, y, width, height);
        if (element->LoadFromFile(filePath)) {
            return element;
        }
        return nullptr;
    }

    inline void RegisterCDRPlugin() {
        UltraCanvasGraphicsPluginRegistry::RegisterPlugin(std::make_shared<UltraCanvasCDRPlugin>());
    }

} // namespace UltraCanvas

#endif // ULTRACANVAS_CDR_PLUGIN_H