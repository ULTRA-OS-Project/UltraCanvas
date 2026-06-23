// Plugins/Models/STL/UltraCanvasSTLPlugin.h
// STL 3D model loader/saver plugin for the UltraCanvas graphics plugin system.
// Registers STL with the graphics plugin registry so it is available through
// FileLoader / LoadGraphicsFile, and exposes a saver API for writing STL.
// Version: 1.0.0
// Last Modified: 2026-06-21
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_STL_PLUGIN_H
#define ULTRACANVAS_STL_PLUGIN_H

#include "UltraCanvasGraphicsPluginSystem.h"
#include "UltraCanvasSTLLoader.h"
#include "UltraCanvas3DTypes.h"
#include <string>
#include <vector>
#include <memory>

namespace UltraCanvas {

// ===== STL PLUGIN =====
    class UltraCanvasSTLPlugin : public IGraphicsPlugin {
    public:
        // ----- Plugin identity -----
        std::string GetPluginName() const override { return "UltraCanvas STL Plugin"; }
        std::string GetPluginVersion() const override { return "1.0.0"; }
        std::vector<std::string> GetSupportedExtensions() const override { return {"stl"}; }

        // ----- File handling (loading) -----
        bool CanHandle(const std::string& filePath) const override;
        bool CanHandle(const GraphicsFileInfo& fileInfo) const override;

        std::shared_ptr<UltraCanvasUIElement> LoadGraphics(const std::string& filePath) override;
        std::shared_ptr<UltraCanvasUIElement> LoadGraphics(const GraphicsFileInfo& fileInfo) override;
        std::shared_ptr<UltraCanvasUIElement> CreateGraphics(int width, int height,
                                                             GraphicsFormatType type) override;

        // ----- Information / capabilities -----
        GraphicsManipulation GetSupportedManipulations() const override;
        GraphicsFileInfo GetFileInfo(const std::string& filePath) override;
        bool ValidateFile(const std::string& filePath) override;

        // ----- Saving (not part of IGraphicsPlugin) -----
        // Save a mesh to an STL file. Convenience entry point so callers/FileLoader
        // can write STL without touching the loader directly.
        static bool SaveModel(const std::string& filePath, const Mesh3D& mesh,
                              STLFormat format = STLFormat::Auto,
                              std::string* outError = nullptr);

        // Load a file straight into a Mesh3D (no UI element involved).
        static bool LoadModel(const std::string& filePath, Mesh3D& outMesh,
                              std::string* outError = nullptr);
    };

// ===== REGISTRATION CONVENIENCE =====
// Call once at startup to make STL available to FileLoader / LoadGraphicsFile.
    inline void RegisterSTLPlugin() {
        UltraCanvasGraphicsPluginRegistry::RegisterPlugin(std::make_shared<UltraCanvasSTLPlugin>());
    }

} // namespace UltraCanvas

#endif // ULTRACANVAS_STL_PLUGIN_H
