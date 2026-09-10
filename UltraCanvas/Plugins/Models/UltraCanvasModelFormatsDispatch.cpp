// UltraCanvas/Plugins/Models/UltraCanvasModelFormatsDispatch.cpp
// Extension dispatch for the 3D converters: file name in, converter or
// document out.
//
// Deliberately separate from UltraCanvasModelFormatsPlugin.cpp, which holds
// the IGraphicsPlugin façade. That one needs the UI stack, because it hands
// back a viewer element; this one needs nothing but the converters and the
// document, so a tool that only converts files links a fraction of the
// framework — and the dispatch stays testable without a display.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "Models/UltraCanvasModelFormatsPlugin.h"

#include "Models/3DS/UltraCanvas3DSConverter.h"
#include "Models/OBJ/UltraCanvasOBJConverter.h"
#include "Models/DXF/UltraCanvasDXFModelConverter.h"
#include "Models/STEP/UltraCanvasStepConverter.h"
#include "Models/Alembic/UltraCanvasAlembicConverter.h"

#ifdef ULTRACANVAS_HAS_COLLADA_CONVERTER
    #include "Models/COLLADA/UltraCanvasColladaConverter.h"
#endif
#ifdef ULTRACANVAS_HAS_X3D_CONVERTER
    #include "Models/X3D/UltraCanvasX3DConverter.h"
#endif
#ifdef ULTRACANVAS_HAS_BLEND_CONVERTER
    #include "Models/Blend/UltraCanvasBlendConverter.h"
#endif

#include <cctype>
#include <iostream>
#include <string>
#include <vector>

namespace UltraCanvas {

using namespace ModelConverter;

namespace {

// The extension, lowercased and without its dot, from an extension or a path.
std::string ExtensionOf(const std::string& extensionOrPath) {
    const size_t slash = extensionOrPath.find_last_of("/\\");
    const std::string name = slash == std::string::npos ? extensionOrPath
                                                        : extensionOrPath.substr(slash + 1);
    const size_t dot = name.rfind('.');
    std::string extension = dot == std::string::npos ? name : name.substr(dot + 1);
    for (char& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return extension;
}

// Warnings from a call that took no options still go somewhere, so a lossy
// conversion is never silent even when the caller did not ask for the
// callback.
ConversionOptions DefaultOptions() {
    ConversionOptions options;
    options.WarningCallback = [](const std::string& message) {
        std::clog << "[Models] " << message << std::endl;
    };
    return options;
}

} // namespace

// ===== DISPATCH =====

std::unique_ptr<IModelFormatConverter>
UltraCanvasModelFormatsPlugin::CreateConverterForExtension(const std::string& extensionOrPath) {
    const std::string extension = ExtensionOf(extensionOrPath);

    if (extension == "3ds") return std::make_unique<ThreeDSConverter>();
    if (extension == "obj") return std::make_unique<OBJConverter>();
    if (extension == "dxf") return std::make_unique<DXFModelConverter>();
    if (extension == "step" || extension == "stp" || extension == "p21")
        return std::make_unique<StepConverter>();
    if (extension == "abc") return std::make_unique<AlembicConverter>();
#ifdef ULTRACANVAS_HAS_COLLADA_CONVERTER
    if (extension == "dae") return std::make_unique<ColladaConverter>();
#endif
#ifdef ULTRACANVAS_HAS_X3D_CONVERTER
    // Only the XML encoding. ".x3dv" and ".wrl" are the classic VRML syntax,
    // which this reader cannot parse, so they are left unclaimed rather than
    // claimed and then refused.
    if (extension == "x3d") return std::make_unique<X3DConverter>();
#endif
#ifdef ULTRACANVAS_HAS_BLEND_CONVERTER
    if (extension == "blend") return std::make_unique<BlendConverter>();
#endif
    return nullptr;
}

std::vector<std::string> UltraCanvasModelFormatsPlugin::SupportedLoadExtensions() {
    // Not "dxf": a DXF is a drawing far more often than a model, so the Vector
    // plugin's reader stays the default for it. CreateConverterForExtension
    // still answers for it, because an explicit caller has already chosen.
    std::vector<std::string> extensions = {"3ds", "obj", "step", "stp", "p21", "abc"};
#ifdef ULTRACANVAS_HAS_COLLADA_CONVERTER
    extensions.push_back("dae");
#endif
#ifdef ULTRACANVAS_HAS_X3D_CONVERTER
    extensions.push_back("x3d");
#endif
#ifdef ULTRACANVAS_HAS_BLEND_CONVERTER
    // Claimed so a file browser can describe a .blend, even though loading it
    // as geometry deliberately yields nothing.
    extensions.push_back("blend");
#endif
    return extensions;
}

std::vector<std::string> UltraCanvasModelFormatsPlugin::SupportedSaveExtensions() {
    return {"obj", "step", "stp"};
}

std::vector<ModelFormat> UltraCanvasModelFormatsPlugin::AvailableFormats() {
    std::vector<ModelFormat> formats = {ModelFormat::ThreeDS, ModelFormat::OBJ,
                                        ModelFormat::DXF, ModelFormat::STEP,
                                        ModelFormat::Alembic};
#ifdef ULTRACANVAS_HAS_COLLADA_CONVERTER
    formats.push_back(ModelFormat::COLLADA);
#endif
#ifdef ULTRACANVAS_HAS_X3D_CONVERTER
    formats.push_back(ModelFormat::X3D);
#endif
#ifdef ULTRACANVAS_HAS_BLEND_CONVERTER
    formats.push_back(ModelFormat::Blend);
#endif
    return formats;
}

std::shared_ptr<ModelStorage::ModelDocument> UltraCanvasModelFormatsPlugin::LoadModelDocument(
        const std::string& filePath) {
    return LoadModelDocument(filePath, DefaultOptions());
}

std::shared_ptr<ModelStorage::ModelDocument> UltraCanvasModelFormatsPlugin::LoadModelDocument(
        const std::string& filePath, const ConversionOptions& options) {
    auto converter = CreateConverterForExtension(filePath);
    if (!converter) {
        // Saying which formats this build does carry is the difference between
        // a user learning that .abc is not supported and a user believing
        // their file is corrupt.
        std::string known;
        for (const std::string& extension : SupportedLoadExtensions())
            known += (known.empty() ? "" : ", ") + extension;
        options.Warn("Models: no reader for '" + ExtensionOf(filePath) +
                     "' in this build; it carries " + known);
        return nullptr;
    }
    if (!converter->CanImport()) {
        options.Warn("Models: " + converter->GetFormatName() +
                     " is recognised but cannot be read");
        return nullptr;
    }
    auto document = converter->Import(filePath, options);

    // Honoured here rather than in each converter, so it means the same thing
    // for every format: a B-rep reader fills ModelDocument::Brep and nothing
    // else, and a caller that wants triangles says so once. The exact bodies
    // stay either way — that is the whole point of holding them.
    if (document && options.TessellateOnImport && !document->Brep.Solids.empty()) {
        std::vector<std::string> problems;
        document->TessellateBreps(options.Tessellation, &problems);
        for (const std::string& problem : problems) options.Warn(problem);
    }
    return document;
}

bool UltraCanvasModelFormatsPlugin::SaveModelDocument(const ModelStorage::ModelDocument& document,
                                                      const std::string& filePath) {
    return SaveModelDocument(document, filePath, DefaultOptions());
}

bool UltraCanvasModelFormatsPlugin::SaveModelDocument(const ModelStorage::ModelDocument& document,
                                                      const std::string& filePath,
                                                      const ConversionOptions& options) {
    auto converter = CreateConverterForExtension(filePath);
    if (!converter) {
        std::string known;
        for (const std::string& extension : SupportedSaveExtensions())
            known += (known.empty() ? "" : ", ") + extension;
        options.Warn("Models: no writer for '" + ExtensionOf(filePath) +
                     "' in this build; it can write " + known);
        return false;
    }
    if (!converter->CanExport()) {
        options.Warn("Models: " + converter->GetFormatName() +
                     " can be read but not written");
        return false;
    }
    return converter->Export(document, filePath, options);
}

// Shared with the IGraphicsPlugin façade in UltraCanvasModelFormatsPlugin.cpp.
std::string ModelFormatsExtensionOf(const std::string& extensionOrPath) {
    return ExtensionOf(extensionOrPath);
}

} // namespace UltraCanvas
