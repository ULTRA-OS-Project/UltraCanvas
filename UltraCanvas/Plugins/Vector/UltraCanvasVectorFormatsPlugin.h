// UltraCanvas/Plugins/Vector/UltraCanvasVectorFormatsPlugin.h
// Graphics plugin exposing the Vector plugin's converter matrix to the
// framework's plugin registry, so every vector format is reachable through
// LoadGraphicsFile / SaveGraphicsFile and the FileLoader format inventory.
//
// Loading covers the formats with a reader (SVG, XAR, EMF, WMF, DXF, DWG*)
// and produces an UltraCanvasVectorElement holding the parsed
// VectorDocument - an editable model, unlike the render-only elements of
// the dedicated XAR/EPS/CDR plugins. Register this plugin alongside those:
// registration order decides which plugin owns a shared load extension
// (last registration wins), while saving always finds this plugin because
// it is the only one that writes vector formats.
//
// Saving covers the whole matrix (SVG, XAR, EPS, CDR, PDF, EMF, WMF, AI,
// DXF, DWG*) from any UltraCanvasVectorElement.
// (*) DWG needs GNU LibreDWG's command-line tools - see
// UltraCanvasCADConverters.h.
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasGraphicsPluginSystem.h"
#include "UltraCanvasVectorConverter.h"
#include "UltraCanvasVectorElement.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

    class UltraCanvasVectorFormatsPlugin : public IGraphicsPlugin {
    public:
        std::string GetPluginName() const override {
            return "UltraCanvas Vector Formats Plugin";
        }
        std::string GetPluginVersion() const override { return "1.0.0"; }

        // Formats with a reader in the converter matrix.
        std::vector<std::string> GetSupportedExtensions() const override {
            return {"svg", "xar", "emf", "wmf", "dxf", "dwg"};
        }
        // The full writer matrix.
        std::vector<std::string> GetSaveExtensions() const override {
            return {"svg", "xar", "eps", "cdr", "pdf", "emf", "wmf", "ai",
                    "dxf", "dwg"};
        }

        bool CanHandle(const std::string& filePath) const override;
        bool CanHandle(const GraphicsFileInfo& fileInfo) const override;

        std::shared_ptr<UltraCanvasUIElement> LoadGraphics(const std::string& filePath) override;
        std::shared_ptr<UltraCanvasUIElement> LoadGraphics(const GraphicsFileInfo& fileInfo) override;
        std::shared_ptr<UltraCanvasUIElement> CreateGraphics(int width, int height,
                                                             GraphicsFormatType type) override;

        GraphicsManipulation GetSupportedManipulations() const override {
            return GraphicsManipulation::Move | GraphicsManipulation::Scale |
                   GraphicsManipulation::Rotate | GraphicsManipulation::Transform;
        }
        GraphicsFileInfo GetFileInfo(const std::string& filePath) override;
        bool ValidateFile(const std::string& filePath) override;

        bool SaveGraphics(const std::shared_ptr<UltraCanvasUIElement>& element,
                          const std::string& filePath) override;

        // ----- Converter access (no UI element involved) -----

        // The converter for a file extension ("svg", ".DXF", or a path);
        // null when no converter covers it.
        static std::unique_ptr<VectorConverter::IVectorFormatConverter>
        CreateConverterForExtension(const std::string& extensionOrPath);

        // Parse a vector file straight into a document / write a document
        // straight to a file, dispatched by extension. Warnings go to the
        // debug log; pass options to receive them yourself.
        static std::shared_ptr<VectorStorage::VectorDocument> LoadVectorDocument(
                const std::string& filePath);
        static bool SaveVectorDocument(const VectorStorage::VectorDocument& document,
                                       const std::string& filePath);
    };

// Call once at startup to make the vector formats available to
// FileLoader / LoadGraphicsFile / SaveGraphicsFile.
    inline void RegisterVectorFormatsPlugin() {
        UltraCanvasGraphicsPluginRegistry::RegisterPlugin(
                std::make_shared<UltraCanvasVectorFormatsPlugin>());
    }

} // namespace UltraCanvas
