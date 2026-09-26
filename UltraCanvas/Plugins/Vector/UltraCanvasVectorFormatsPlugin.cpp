// UltraCanvas/Plugins/Vector/UltraCanvasVectorFormatsPlugin.cpp
// Implementation of the vector formats graphics plugin - see the header.
// Version: 1.2.0
// Last Modified: 2026-09-26
// Author: UltraCanvas Framework

#include "UltraCanvasVectorFormatsPlugin.h"

#include "UltraCanvasCADConverters.h"
#include "UltraCanvasCDRConverter.h"
#include "UltraCanvasEPSConverter.h"
#include "UltraCanvasMetafileConverters.h"
#include "UltraCanvasXARConverter.h"
#include "UltraCanvasVectorPreview.h"

#include <algorithm>
#include <fstream>

namespace UltraCanvas {

using namespace VectorConverter;

namespace {

std::string ExtensionOf(const std::string& path) {
    size_t dot = path.find_last_of('.');
    std::string ext = dot == std::string::npos ? path : path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

ConversionOptions DebugLogOptions() {
    ConversionOptions options;
    options.WarningCallback = [](const std::string& msg) {
        debugOutput << "VectorFormatsPlugin: " << msg << std::endl;
    };
    return options;
}

}   // namespace

namespace {

// The one list of converters. Everything else - which converter a path
// gets, which extensions are readable, which writable - is asked of these.
std::vector<std::unique_ptr<IVectorFormatConverter>> AllConverters() {
    std::vector<std::unique_ptr<IVectorFormatConverter>> all;
    all.push_back(std::make_unique<SVGConverter>());
    all.push_back(std::make_unique<XARConverter>());
    all.push_back(std::make_unique<EPSConverter>());
    all.push_back(std::make_unique<CDRConverter>());
    all.push_back(std::make_unique<PDFVectorConverter>());
    all.push_back(std::make_unique<EMFConverter>());
    all.push_back(std::make_unique<WMFConverter>());
    all.push_back(std::make_unique<AIConverter>());
    all.push_back(std::make_unique<DXFConverter>());
    all.push_back(std::make_unique<DWGConverter>());
    return all;
}

// ".SVG" / "svg" -> "svg".
std::string NormalizedExtension(std::string ext) {
    if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

std::vector<std::string> ExtensionsWhere(bool (IVectorFormatConverter::*can)() const) {
    std::vector<std::string> out;
    for (const auto& converter : AllConverters()) {
        if (!((*converter).*can)()) continue;
        for (const std::string& e : converter->GetFileExtensions()) {
            std::string ext = NormalizedExtension(e);
            if (!ext.empty() && std::find(out.begin(), out.end(), ext) == out.end())
                out.push_back(ext);
        }
    }
    return out;
}

}   // namespace

std::vector<std::string> UltraCanvasVectorFormatsPlugin::GetSupportedExtensions() const {
    return ExtensionsWhere(&IVectorFormatConverter::CanImport);
}

std::vector<std::string> UltraCanvasVectorFormatsPlugin::GetSaveExtensions() const {
    return ExtensionsWhere(&IVectorFormatConverter::CanExport);
}

std::unique_ptr<IVectorFormatConverter>
UltraCanvasVectorFormatsPlugin::CreateConverterForExtension(
        const std::string& extensionOrPath) {
    const std::string ext = NormalizedExtension(ExtensionOf(extensionOrPath));
    if (ext.empty()) return nullptr;
    for (auto& converter : AllConverters()) {
        for (const std::string& e : converter->GetFileExtensions()) {
            if (NormalizedExtension(e) == ext) return std::move(converter);
        }
    }
    // A .bak is AutoCAD's verbatim copy of a drawing - but it is also what
    // every other program calls its backups, so this one is claimed on its
    // content, not its name: only a file that actually carries the AC10xx
    // header gets the DWG converter. A bare extension has no file to read
    // and is declined, which is why .bak is never an advertised extension.
    if (DWGConverter::IsAmbiguousDrawingExtension(ext) &&
        DWGConverter().ValidateFile(extensionOrPath))
        return std::make_unique<DWGConverter>();
    return nullptr;
}

std::shared_ptr<VectorStorage::VectorDocument>
UltraCanvasVectorFormatsPlugin::LoadVectorDocument(const std::string& filePath) {
    auto converter = CreateConverterForExtension(filePath);
    if (!converter || !converter->CanImport()) return nullptr;
    return converter->Import(filePath, DebugLogOptions());
}

bool UltraCanvasVectorFormatsPlugin::SaveVectorDocument(
        const VectorStorage::VectorDocument& document, const std::string& filePath) {
    auto converter = CreateConverterForExtension(filePath);
    if (!converter || !converter->CanExport()) return false;
    return converter->Export(document, filePath, DebugLogOptions());
}

bool UltraCanvasVectorFormatsPlugin::CanHandle(const std::string& filePath) const {
    auto converter = CreateConverterForExtension(filePath);
    return converter && converter->CanImport();
}

bool UltraCanvasVectorFormatsPlugin::CanHandle(const GraphicsFileInfo& fileInfo) const {
    // The path when the info carries one: an extension alone cannot answer
    // for the formats decided by content (.bak), and the answer here must
    // match what LoadGraphics() will do with the same file.
    return CanHandle(fileInfo.filename.empty() ? "." + fileInfo.extension
                                               : fileInfo.filename);
}

std::shared_ptr<UltraCanvasUIElement>
UltraCanvasVectorFormatsPlugin::LoadGraphics(const std::string& filePath) {
    auto doc = LoadVectorDocument(filePath);
    if (!doc) return nullptr;
    double w = doc->Size.width > 0 ? doc->Size.width : 400;
    double h = doc->Size.height > 0 ? doc->Size.height : 300;
    auto element = CreateVectorElement("vector_" + filePath, 0, 0,
                                       static_cast<int>(std::lround(w)),
                                       static_cast<int>(std::lround(h)));
    element->SetDocument(doc);
    return element;
}

std::shared_ptr<UltraCanvasUIElement>
UltraCanvasVectorFormatsPlugin::LoadGraphics(const GraphicsFileInfo& fileInfo) {
    return LoadGraphics(fileInfo.filename);
}

std::shared_ptr<UltraCanvasUIElement>
UltraCanvasVectorFormatsPlugin::CreateGraphics(int width, int height,
                                               GraphicsFormatType type) {
    if (type != GraphicsFormatType::Vector) return nullptr;
    auto element = CreateVectorElement("vector_new", 0, 0,
                                       width > 0 ? width : 400,
                                       height > 0 ? height : 300);
    auto doc = std::make_shared<VectorStorage::VectorDocument>();
    doc->Size = Size2Dd{static_cast<double>(width > 0 ? width : 400),
                        static_cast<double>(height > 0 ? height : 300)};
    doc->AddLayer("Layer 1");
    element->SetDocument(doc);
    return element;
}

GraphicsFileInfo UltraCanvasVectorFormatsPlugin::GetFileInfo(const std::string& filePath) {
    GraphicsFileInfo info(filePath);
    info.formatType = GraphicsFormatType::Vector;
    info.supportedManipulations = GetSupportedManipulations();
    std::ifstream in(filePath, std::ios::binary | std::ios::ate);
    if (in) {
        std::streamsize fileSize = in.tellg();
        if (fileSize > 0) info.fileSize = static_cast<size_t>(fileSize);
    }
    return info;
}

bool UltraCanvasVectorFormatsPlugin::ValidateFile(const std::string& filePath) {
    auto converter = CreateConverterForExtension(filePath);
    return converter && converter->ValidateFile(filePath);
}

void RegisterVectorFormatsPlugin() {
    UltraCanvasGraphicsPluginRegistry::RegisterPlugin(
            std::make_shared<UltraCanvasVectorFormatsPlugin>());

    // The core-side seam. The graphics registry hands back a UI element, which
    // is the wrong shape for a preview: the media viewer and the Filer want
    // the document, so they can draw it at whatever size the pane or the tile
    // happens to be. Core owns the document model and the renderer but not one
    // reader, so this is where the readers are handed over.
    VectorPreviewProvider provider;
    provider.Extensions = []() {
        return UltraCanvasVectorFormatsPlugin().GetSupportedExtensions();
    };
    provider.Load = [](const std::string& path)
            -> std::shared_ptr<VectorStorage::VectorDocument> {
        return UltraCanvasVectorFormatsPlugin::LoadVectorDocument(path);
    };
    // The formats whose suffix settles nothing: a .bak is a drawing only when
    // its header says so (UltraCanvasCADConverters.h).
    provider.ClaimsFile = [](const std::string& path) {
        auto converter = UltraCanvasVectorFormatsPlugin::CreateConverterForExtension(path);
        return converter && converter->CanImport();
    };
    // The editing half, for UltraCanvasFileLoader::LoadVectorDocument /
    // SaveVectorDocument: the same readers with their notes, and the writers.
    provider.Import = [](const std::string& path, const VectorPreviewProvider::NoteFn& note)
            -> std::shared_ptr<VectorStorage::VectorDocument> {
        auto converter = UltraCanvasVectorFormatsPlugin::CreateConverterForExtension(path);
        if (!converter || !converter->CanImport()) return nullptr;
        VectorConverter::ConversionOptions options;
        options.WarningCallback = note;
        return converter->Import(path, options);
    };
    provider.SaveExtensions = []() {
        return UltraCanvasVectorFormatsPlugin().GetSaveExtensions();
    };
    provider.Save = [](const VectorStorage::VectorDocument& document, const std::string& path,
                       const VectorPreviewProvider::NoteFn& note) {
        auto converter = UltraCanvasVectorFormatsPlugin::CreateConverterForExtension(path);
        if (!converter || !converter->CanExport()) return false;
        VectorConverter::ConversionOptions options;
        options.WarningCallback = note;
        return converter->Export(document, path, options);
    };
    SetVectorPreviewProvider(std::move(provider));
}

bool UltraCanvasVectorFormatsPlugin::SaveGraphics(
        const std::shared_ptr<UltraCanvasUIElement>& element,
        const std::string& filePath) {
    auto vectorElement = std::dynamic_pointer_cast<UltraCanvasVectorElement>(element);
    if (!vectorElement || !vectorElement->HasDocument()) return false;
    return SaveVectorDocument(*vectorElement->GetDocument(), filePath);
}

} // namespace UltraCanvas
