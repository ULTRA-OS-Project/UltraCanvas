// UltraCanvas/Plugins/Models/UltraCanvasModelFormatsPlugin.h
// Graphics plugin exposing the Models plugin's converter matrix to the
// framework's plugin registry, so every 3D format is reachable through
// LoadGraphicsFile / SaveGraphicsFile and the FileLoader format inventory.
//
// The 2D counterpart is UltraCanvasVectorFormatsPlugin, and this is
// deliberately the same shape: one façade, extension dispatch through
// CreateConverterForExtension, and an IGraphicsPlugin implementation so the
// Model3D category stops being STL-only.
//
// Which converters exist depends on how the plugin was built — COLLADA needs
// tinyxml2 and .blend inspection needs zlib, so both are compile-time
// optional. Ask GetSupportedExtensions rather than assuming.
//
// One extension is deliberately NOT claimed: .dxf. A DXF is a drawing far more
// often than a model, and the Vector plugin's reader is the right default for
// it; a caller that wants the 3D entities asks for the converter by name
// (CreateConverterForExtension(".dxf")) or uses DXFModelConverter directly.
// Registration order would otherwise decide it silently, last registration
// winning.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_MODEL_FORMATS_PLUGIN_H
#define ULTRACANVAS_MODEL_FORMATS_PLUGIN_H

#include "UltraCanvasGraphicsPluginSystem.h"
#include "DataFormats/UltraCanvasModelConverter.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

    class UltraCanvasModelFormatsPlugin : public IGraphicsPlugin {
    public:
        std::string GetPluginName() const override {
            return "UltraCanvas Model Formats Plugin";
        }
        std::string GetPluginVersion() const override { return "1.0.0"; }

        std::vector<std::string> GetSupportedExtensions() const override;
        std::vector<std::string> GetSaveExtensions() const override;

        bool CanHandle(const std::string& filePath) const override;
        bool CanHandle(const GraphicsFileInfo& fileInfo) const override;

        // Loads the model and hands back a viewer: the document is flattened
        // to a mesh and given to an UltraCanvasSTLElement, which shades it on
        // GL builds and summarises it otherwise. A format that declines to
        // import (.blend) returns null — GetFileInfo still describes it.
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

        // The formats this build can read, and the ones it can write. Static,
        // and defined beside the dispatch rather than beside the plugin, so a
        // caller can ask what is supported without linking the UI — the
        // virtuals above just forward here, keeping one source of truth.
        //
        // STL is absent because UltraCanvasSTLPlugin owns it;
        // RegisterModelFormatsPlugin registers both, so a caller gets the
        // whole Model3D category either way.
        static std::vector<std::string> SupportedLoadExtensions();
        static std::vector<std::string> SupportedSaveExtensions();

        // The converter for a file extension ("obj", ".DAE", or a path); null
        // when this build has none for it. Unlike GetSupportedExtensions this
        // does answer for ".dxf", because an explicit caller has already said
        // it wants the model reader rather than the drawing one.
        static std::unique_ptr<ModelConverter::IModelFormatConverter>
        CreateConverterForExtension(const std::string& extensionOrPath);

        // Read a model file straight into a document / write a document
        // straight to a file, dispatched by extension. Warnings go to the
        // debug log; pass options to receive them yourself.
        static std::shared_ptr<ModelStorage::ModelDocument> LoadModelDocument(
                const std::string& filePath);
        static std::shared_ptr<ModelStorage::ModelDocument> LoadModelDocument(
                const std::string& filePath, const ModelConverter::ConversionOptions& options);
        static bool SaveModelDocument(const ModelStorage::ModelDocument& document,
                                      const std::string& filePath);
        static bool SaveModelDocument(const ModelStorage::ModelDocument& document,
                                      const std::string& filePath,
                                      const ModelConverter::ConversionOptions& options);

        // Every format this build carries, readable or not, for a UI that
        // wants to list what it supports.
        static std::vector<ModelConverter::ModelFormat> AvailableFormats();
    };

// Call once at startup to make every 3D format available to FileLoader /
// LoadGraphicsFile. Registers the STL plugin too, so one call covers the whole
// Model3D category — STL was previously reachable only if a particular demo
// page had been opened.
    void RegisterModelFormatsPlugin();

} // namespace UltraCanvas

#endif // ULTRACANVAS_MODEL_FORMATS_PLUGIN_H
