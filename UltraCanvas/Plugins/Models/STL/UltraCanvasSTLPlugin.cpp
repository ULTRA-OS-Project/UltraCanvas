// Plugins/Models/STL/UltraCanvasSTLPlugin.cpp
// Implementation of the STL graphics plugin
// Version: 1.0.0
// Last Modified: 2026-06-21
// Author: UltraCanvas Framework

#include "UltraCanvasSTLPlugin.h"
#include "UltraCanvasSTLElement.h"

#include <fstream>

namespace UltraCanvas {

namespace {
    constexpr float kDefaultViewerWidth = 400.0f;
    constexpr float kDefaultViewerHeight = 300.0f;
}

bool UltraCanvasSTLPlugin::CanHandle(const std::string& filePath) const {
    return UltraCanvasSTLLoader::HasSTLExtension(filePath);
}

bool UltraCanvasSTLPlugin::CanHandle(const GraphicsFileInfo& fileInfo) const {
    return fileInfo.extension == "stl" ||
           fileInfo.formatType == GraphicsFormatType::ThreeD;
}

std::shared_ptr<UltraCanvasUIElement>
UltraCanvasSTLPlugin::LoadGraphics(const std::string& filePath) {
    auto element = std::make_shared<UltraCanvasSTLElement>(
        "stl_" + filePath, 0, 0, kDefaultViewerWidth, kDefaultViewerHeight);
    if (!element->LoadFromFile(filePath)) {
        return nullptr;
    }
    return element;
}

std::shared_ptr<UltraCanvasUIElement>
UltraCanvasSTLPlugin::LoadGraphics(const GraphicsFileInfo& fileInfo) {
    return LoadGraphics(fileInfo.filename);
}

std::shared_ptr<UltraCanvasUIElement>
UltraCanvasSTLPlugin::CreateGraphics(int width, int height, GraphicsFormatType type) {
    if (type != GraphicsFormatType::ThreeD) return nullptr;
    float w = (width > 0) ? static_cast<float>(width) : kDefaultViewerWidth;
    float h = (height > 0) ? static_cast<float>(height) : kDefaultViewerHeight;
    return std::make_shared<UltraCanvasSTLElement>("stl_new", 0, 0, w, h);
}

GraphicsManipulation UltraCanvasSTLPlugin::GetSupportedManipulations() const {
    // The 3D viewer supports orbit/zoom (rotate + scale), not 2D crop/filter.
    return GraphicsManipulation::Move |
           GraphicsManipulation::Rotate |
           GraphicsManipulation::Scale;
}

GraphicsFileInfo UltraCanvasSTLPlugin::GetFileInfo(const std::string& filePath) {
    GraphicsFileInfo info(filePath);
    info.formatType = GraphicsFormatType::ThreeD;
    info.supportedManipulations = GetSupportedManipulations();

    std::ifstream in(filePath, std::ios::binary | std::ios::ate);
    if (in) {
        std::streamsize size = in.tellg();
        if (size > 0) info.fileSize = static_cast<size_t>(size);
    }

    Mesh3D mesh;
    if (UltraCanvasSTLLoader::Load(filePath, mesh, nullptr)) {
        info.metadata["triangles"] = std::to_string(mesh.TriangleCount());
        info.metadata["vertices"] = std::to_string(mesh.VertexCount());
        if (mesh.bounds.IsValid()) {
            Vec3 d = mesh.bounds.max - mesh.bounds.min;
            info.metadata["bbox"] = std::to_string(d.x) + " x " +
                                    std::to_string(d.y) + " x " +
                                    std::to_string(d.z);
        }
    }
    return info;
}

bool UltraCanvasSTLPlugin::ValidateFile(const std::string& filePath) {
    if (!UltraCanvasSTLLoader::HasSTLExtension(filePath)) return false;
    Mesh3D mesh;
    return UltraCanvasSTLLoader::Load(filePath, mesh, nullptr);
}

bool UltraCanvasSTLPlugin::SaveModel(const std::string& filePath, const Mesh3D& mesh,
                                     STLFormat format, std::string* outError) {
    return UltraCanvasSTLLoader::Save(filePath, mesh, format, outError);
}

bool UltraCanvasSTLPlugin::LoadModel(const std::string& filePath, Mesh3D& outMesh,
                                     std::string* outError) {
    return UltraCanvasSTLLoader::Load(filePath, outMesh, outError);
}

} // namespace UltraCanvas
