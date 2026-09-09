// Plugins/Vector/EPS/UltraCanvasEPSPlugin.h
// Encapsulated PostScript (EPS) vector graphics plugin for UltraCanvas.
//
// EPS files are PostScript programs: real-world files (Illustrator, Corel,
// ghostscript, cairo, ...) define their own procedures in a prolog and draw
// through them, so a fixed operator table cannot render them. This plugin
// therefore embeds a small PostScript interpreter — scanner, operand /
// dictionary / execution stacks, procedures, control flow, the graphics and
// path operators — and plays the program back through IRenderContext.
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasGraphicsPluginSystem.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// ===== PARSE DIAGNOSTICS =====

    // Triage data for "the file displays wrong": operators the interpreter
    // does not implement (with use counts) point at missing features, and
    // warnings mark places where it recovered from a defect or fell back to
    // an approximation.
    struct EPSParseDiagnostics {
        size_t tokenCount = 0;
        std::map<std::string, size_t> unknownOperators;
        std::vector<std::string> warnings;
        void Warn(const std::string& msg) {
            if (warnings.size() < 64) warnings.push_back(msg);
        }
    };

// ===== EPS DOCUMENT =====

    class EPSDocument {
    public:
        EPSDocument() = default;

        bool LoadFromFile(const std::string& filepath);
        bool LoadFromMemory(const uint8_t* data, size_t size);

        // Page size in points/pixels, from %%(HiRes)BoundingBox; falls back
        // to US Letter when the file declares none.
        float GetWidth() const { return static_cast<float>(bboxW); }
        float GetHeight() const { return static_cast<float>(bboxH); }

        // Interpret the PostScript program against the context. The origin
        // is the bounding box's lower-left corner mapped to the page's
        // bottom, i.e. the drawing appears exactly as the BoundingBox crops
        // it. Pass scale to rasterize larger or smaller.
        void Render(IRenderContext* ctx, float scale = 1.0f);

        const EPSParseDiagnostics& GetDiagnostics() const { return diagnostics; }

        const std::string& GetTitle() const { return title; }
        const std::string& GetCreator() const { return creator; }

    private:
        // The PostScript program text (already unwrapped from a DOS EPS
        // binary preview header when present).
        std::string source;
        double bboxX = 0, bboxY = 0, bboxW = 612, bboxH = 792;
        bool hasBBox = false;
        std::string title;
        std::string creator;
        EPSParseDiagnostics diagnostics;

        bool ParseHeader();
    };

// ===== EPS UI ELEMENT =====

    class UltraCanvasEPSElement : public UltraCanvasUIElement {
    public:
        UltraCanvasEPSElement(const std::string& identifier,
                              float x, float y, float w, float h);
        virtual ~UltraCanvasEPSElement() = default;

        bool LoadFromFile(const std::string& filepath);
        bool LoadFromMemory(const uint8_t* data, size_t size);
        bool IsLoaded() const { return document != nullptr; }

        // Reason for the most recent failed load (missing / unreadable /
        // not PostScript). Empty after a successful load.
        const std::string& GetLastError() const { return lastError; }

        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;

        void SetScale(float s) { scale = s; }
        float GetScale() const { return scale; }
        void SetPreserveAspectRatio(bool preserve) { preserveAspectRatio = preserve; }
        bool GetPreserveAspectRatio() const { return preserveAspectRatio; }

        const EPSDocument* GetDocument() const { return document.get(); }

    private:
        std::unique_ptr<EPSDocument> document;
        std::string lastError;
        float scale = 1.0f;
        bool preserveAspectRatio = true;
    };

// ===== PLUGIN =====

    class UltraCanvasEPSPlugin : public IGraphicsPlugin {
    public:
        UltraCanvasEPSPlugin() = default;
        ~UltraCanvasEPSPlugin() override = default;

        std::string GetPluginName() const override { return "UltraCanvas EPS Plugin"; }
        std::string GetPluginVersion() const override;
        std::vector<std::string> GetSupportedExtensions() const override {
            return {"eps", "epsf", "ps"};
        }

        bool CanHandle(const std::string& filePath) const override;
        bool CanHandle(const GraphicsFileInfo& fileInfo) const override;

        std::shared_ptr<UltraCanvasUIElement> LoadGraphics(const std::string& filePath) override;
        std::shared_ptr<UltraCanvasUIElement> LoadGraphics(const GraphicsFileInfo& fileInfo) override;
        std::shared_ptr<UltraCanvasUIElement> CreateGraphics(int width, int height,
                                                             GraphicsFormatType type) override;

        GraphicsManipulation GetSupportedManipulations() const override {
            return GraphicsManipulation::Move | GraphicsManipulation::Scale;
        }
        GraphicsFileInfo GetFileInfo(const std::string& filePath) override;
        bool ValidateFile(const std::string& filePath) override;

    private:
        std::string GetFileExtension(const std::string& filePath) const;
    };

    inline std::shared_ptr<UltraCanvasEPSPlugin> CreateEPSPlugin() {
        return std::make_shared<UltraCanvasEPSPlugin>();
    }

    inline void RegisterEPSPlugin() {
        UltraCanvasGraphicsPluginRegistry::RegisterPlugin(CreateEPSPlugin());
    }

} // namespace UltraCanvas
