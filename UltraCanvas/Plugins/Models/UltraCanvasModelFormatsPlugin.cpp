// UltraCanvas/Plugins/Models/UltraCanvasModelFormatsPlugin.cpp
// The IGraphicsPlugin façade for the 3D converters: what makes a model file
// reachable through LoadGraphicsFile / SaveGraphicsFile and the FileLoader
// format inventory.
//
// Extension dispatch itself lives in UltraCanvasModelFormatsDispatch.cpp,
// which has no UI dependency.
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "Models/UltraCanvasModelFormatsPlugin.h"

#include "Models/UltraCanvasModelMesh3D.h"
#include "Models/STL/UltraCanvasSTLElement.h"
#include "Models/STL/UltraCanvasSTLPlugin.h"

#ifdef ULTRACANVAS_HAS_BLEND_CONVERTER
    #include "Models/Blend/UltraCanvasBlendConverter.h"
#endif

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasModelPreview.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace UltraCanvas {

using namespace ModelConverter;

// Defined in UltraCanvasModelFormatsDispatch.cpp.
std::string ModelFormatsExtensionOf(const std::string& extensionOrPath);

namespace {

std::string ExtensionOf(const std::string& path) { return ModelFormatsExtensionOf(path); }

} // namespace

// ===== IGraphicsPlugin =====

std::vector<std::string> UltraCanvasModelFormatsPlugin::GetSupportedExtensions() const {
    return SupportedLoadExtensions();
}

std::vector<std::string> UltraCanvasModelFormatsPlugin::GetSaveExtensions() const {
    return SupportedSaveExtensions();
}

bool UltraCanvasModelFormatsPlugin::CanHandle(const std::string& filePath) const {
    const std::string extension = ExtensionOf(filePath);
    const std::vector<std::string> supported = GetSupportedExtensions();
    return std::find(supported.begin(), supported.end(), extension) != supported.end();
}

bool UltraCanvasModelFormatsPlugin::CanHandle(const GraphicsFileInfo& fileInfo) const {
    return CanHandle("." + fileInfo.extension);
}

std::shared_ptr<UltraCanvasUIElement> UltraCanvasModelFormatsPlugin::LoadGraphics(
        const std::string& filePath) {
    ConversionOptions options;
    // A viewer needs triangles, so a format that carries only exact bodies
    // (STEP, IGES, a DWG 3DSOLID) is tessellated on the way in. The default
    // tolerance is derived from each body's own size, which is what a viewer
    // wants and what saves a millimetre-versus-metre model from arriving as
    // either one triangle or a million.
    options.TessellateOnImport = true;
    auto document = LoadModelDocument(filePath, options);
    if (!document) return nullptr;

    // The document is a scene; a viewer wants one buffer. Flattening applies
    // every node transform, so what is drawn is what the file describes.
    const Mesh3D mesh = ModelDocumentToMesh3D(*document);
    if (mesh.Empty()) return nullptr;

    auto element = std::make_shared<UltraCanvasSTLElement>(
            "model_" + filePath, 0, 0, 640, 480);
    element->SetMesh(mesh);
    return element;
}

std::shared_ptr<UltraCanvasUIElement> UltraCanvasModelFormatsPlugin::LoadGraphics(
        const GraphicsFileInfo& fileInfo) {
    return LoadGraphics(fileInfo.filename);
}

std::shared_ptr<UltraCanvasUIElement> UltraCanvasModelFormatsPlugin::CreateGraphics(
        int width, int height, GraphicsFormatType type) {
    if (type != GraphicsFormatType::ThreeD) return nullptr;
    return std::make_shared<UltraCanvasSTLElement>(
            "model_new", 0, 0, static_cast<float>(width > 0 ? width : 640),
            static_cast<float>(height > 0 ? height : 480));
}

GraphicsFileInfo UltraCanvasModelFormatsPlugin::GetFileInfo(const std::string& filePath) {
    GraphicsFileInfo info(filePath);
    info.formatType = GraphicsFormatType::ThreeD;
    info.supportedManipulations = GetSupportedManipulations();

    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (file) {
        const std::streamoff size = file.tellg();
        if (size > 0) info.fileSize = static_cast<size_t>(size);
    }

    auto converter = CreateConverterForExtension(filePath);
    if (!converter) return info;
    info.mimeType = converter->GetMimeType();
    info.metadata["format"] = converter->GetFormatName();

#ifdef ULTRACANVAS_HAS_BLEND_CONVERTER
    // A .blend never yields a document, so its description comes from the
    // inspector instead — which is the whole point of claiming the extension.
    if (ExtensionOf(filePath) == "blend") {
        const BlendFileInfo blend = BlendConverter::Inspect(filePath);
        if (blend.Valid) {
            info.metadata["blenderVersion"] = blend.Version;
            info.metadata["objects"] = std::to_string(blend.ObjectNames.size());
            info.metadata["meshes"] = std::to_string(blend.MeshNames.size());
            info.metadata["storedVertices"] = std::to_string(blend.StoredVertexCount);
            info.metadata["note"] = blend.Summary();
        }
        return info;
    }
#endif

    // Everything else is described from the document it actually produces.
    ConversionOptions options;
    auto document = converter->CanImport() ? converter->Import(filePath, options) : nullptr;
    if (!document) return info;

    if (!document->Brep.Solids.empty()) {
        // An exact body has no vertex or face count until someone chooses a
        // tolerance, so the browser is told what the file actually holds.
        info.metadata["solids"] = std::to_string(document->Brep.Solids.size());
        info.metadata["brepFaces"] = std::to_string(document->Brep.Faces.size());
        info.metadata["surfaces"] = std::to_string(document->Brep.Surfaces.size());
    }
    info.metadata["vertices"] = std::to_string(document->TotalVertexCount());
    info.metadata["faces"] = std::to_string(document->TotalFaceCount());
    info.metadata["meshes"] = std::to_string(document->Meshes.size());
    info.metadata["materials"] = std::to_string(document->Materials.size());
    if (!document->Animations.empty())
        info.metadata["animations"] = std::to_string(document->Animations.size());
    if (document->SourceUnit != ModelStorage::ModelUnit::Unspecified)
        info.metadata["unit"] = ModelStorage::ModelUnitSymbol(document->SourceUnit);
    info.metadata["upAxis"] = document->Up == ModelStorage::UpAxis::ZUp ? "Z" : "Y";

    const ModelStorage::Bounds3D bounds = document->ComputeBounds();
    if (bounds.IsValid()) {
        const ModelStorage::Vec3d size = bounds.Size();
        info.metadata["bbox"] = std::to_string(size.x) + " x " + std::to_string(size.y) +
                                " x " + std::to_string(size.z);
        // A 3D model has no pixel size; the bounds are what a browser can show
        // instead, rounded so the fields mean something.
        info.width = static_cast<int>(std::lround(size.x));
        info.height = static_cast<int>(std::lround(size.y));
        info.depth = static_cast<int>(std::lround(size.z));
    }
    return info;
}

bool UltraCanvasModelFormatsPlugin::ValidateFile(const std::string& filePath) {
    auto converter = CreateConverterForExtension(filePath);
    // Signature, not extension: a .obj that is really an STL should not pass.
    return converter && converter->ValidateFile(filePath);
}

bool UltraCanvasModelFormatsPlugin::SaveGraphics(
        const std::shared_ptr<UltraCanvasUIElement>& element, const std::string& filePath) {
    auto viewer = std::dynamic_pointer_cast<UltraCanvasSTLElement>(element);
    if (!viewer) return false;
    const ModelStorage::ModelDocument document = Mesh3DToModelDocument(viewer->GetMesh());
    return SaveModelDocument(document, filePath);
}

// ===== REGISTRATION =====

void RegisterModelFormatsPlugin() {
    UltraCanvasGraphicsPluginRegistry::RegisterPlugin(
            std::make_shared<UltraCanvasModelFormatsPlugin>());
    // STL lives in core because the Filer's thumbnails need it, but its plugin
    // had exactly one caller — a demo page — so .stl was invisible to
    // FileLoader until that page was opened. Registering it here makes one
    // call cover the whole Model3D category.
    RegisterSTLPlugin();

    // And teach core to *display* the formats it cannot read. The Filer's
    // thumbnails and the media viewer's 3D pane ask
    // UltraCanvasModelPreview.h, which knows only STL until something fills
    // this in; core cannot call the plugin, because the plugin links against
    // core. See that header for why the seam is shaped this way.
    ModelPreviewProvider provider;
    provider.Extensions = [] {
        return UltraCanvasModelFormatsPlugin::SupportedLoadExtensions();
    };
    provider.Load = [](const std::string& path, Mesh3D& out) {
        // Warnings are the caller's business on a load it asked for; a
        // thumbnail is not, so this one stays quiet rather than logging once
        // per tile per folder listing.
        ConversionOptions options;
        options.TriangulateOnImport = true;
        // A format that carries only exact bodies (STEP, and a DWG's 3DSOLID)
        // has no triangles until someone asks for them, and a preview is
        // exactly the caller that should ask.
        options.TessellateOnImport = true;
        auto document =
                UltraCanvasModelFormatsPlugin::LoadModelDocument(path, options);
        if (!document || document->Empty()) return false;
        out = ModelDocumentToMesh3D(*document);
        return !out.Empty();
    };
    SetModelPreviewProvider(std::move(provider));
}

} // namespace UltraCanvas
