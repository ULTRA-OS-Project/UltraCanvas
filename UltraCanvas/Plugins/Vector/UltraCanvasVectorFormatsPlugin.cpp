// UltraCanvas/Plugins/Vector/UltraCanvasVectorFormatsPlugin.cpp
// Implementation of the vector formats graphics plugin - see the header.
// Version: 1.6.0 - PDF opens for editing (the preview provider's ImportExtensions)
//                  and is nothing else here: not a previewed drawing, not a graphics file
// Version: 1.5.0 - GetFileInfo reports the page (width / height, in points): read, not left at 0
// Version: 1.4.0 - OpenDocument Drawing (.odg / .otg / .fodg) read and written
// Version: 1.3.0 - the preview provider reads from memory too (ImportFromString)
// Version: 1.2.0
// Last Modified: 2026-11-28
// Author: UltraCanvas Framework

#include "UltraCanvasVectorFormatsPlugin.h"

#include "UltraCanvasCADConverters.h"
#include "UltraCanvasCDRConverter.h"
#include "UltraCanvasEPSConverter.h"
#include "UltraCanvasMetafileConverters.h"
#include "UltraCanvasODGConverter.h"
#include "UltraCanvasXARConverter.h"
#include "UltraCanvasVectorPreview.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include "UltraCanvasPathUtf8.h"

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
    all.push_back(std::make_unique<ODGConverter>());
    return all;
}

// ".SVG" / "svg" -> "svg".
std::string NormalizedExtension(std::string ext) {
    if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

// Read only when a drawing program opens one: a PDF is a document with pages
// to the media viewer, the Filer and the image loader, which show it through
// the PDF plugin, and a drawing only in ArtCreator's hands. So it is left out
// of what the graphics registry and the preview seam are told this plugin
// reads, and handed over as the provider's ImportExtensions instead.
bool IsOpenOnlyExtension(const std::string& ext) { return ext == "pdf"; }

std::vector<std::string> ExtensionsWhere(bool (IVectorFormatConverter::*can)() const, bool openOnly = false) {
    std::vector<std::string> out;
    for (const auto& converter : AllConverters()) {
        if (!((*converter).*can)()) continue;
        for (const std::string& e : converter->GetFileExtensions()) {
            std::string ext = NormalizedExtension(e);
            if (ext.empty() || std::find(out.begin(), out.end(), ext) != out.end()) continue;
            if (can == &IVectorFormatConverter::CanImport && IsOpenOnlyExtension(ext) != openOnly) continue;
            out.push_back(ext);
        }
    }
    return out;
}

// A path or extension the previews and the graphics registry leave alone.
bool IsOpenOnlyPath(const std::string& extensionOrPath) {
    return IsOpenOnlyExtension(NormalizedExtension(ExtensionOf(extensionOrPath)));
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
    if (IsOpenOnlyPath(filePath)) return nullptr;
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
    if (IsOpenOnlyPath(filePath)) return false;
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
    std::ifstream in(UltraCanvas::PathFromUtf8(filePath), std::ios::binary | std::ios::ate);
    if (in) {
        std::streamsize fileSize = in.tellg();
        if (fileSize > 0) info.fileSize = static_cast<size_t>(fileSize);
    }
    // The page, in points: these formats say their size only once read.
    if (auto doc = LoadVectorDocument(filePath)) {
        const double w = doc->ViewBox.width > 0 && doc->ViewBox.height > 0 ? doc->ViewBox.width : doc->Size.width;
        const double h = doc->ViewBox.width > 0 && doc->ViewBox.height > 0 ? doc->ViewBox.height : doc->Size.height;
        if (w > 0 && h > 0 && std::isfinite(w) && std::isfinite(h) && w < 1e7 && h < 1e7) {
            info.width = std::max(1, static_cast<int>(std::lround(w)));
            info.height = std::max(1, static_cast<int>(std::lround(h)));
        }
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
        if (IsOpenOnlyPath(path)) return false;
        auto converter = UltraCanvasVectorFormatsPlugin::CreateConverterForExtension(path);
        return converter && converter->CanImport();
    };
    // From bytes: what UCImage::LoadFromMemory asks for an SVG that came
    // without a file, in a build whose libvips cannot rasterize SVG.
    provider.LoadFromMemory = [](const std::string& extension, const uint8_t* data, size_t size)
            -> std::shared_ptr<VectorStorage::VectorDocument> {
        if (IsOpenOnlyPath("." + NormalizedExtension(extension))) return nullptr;
        auto converter = UltraCanvasVectorFormatsPlugin::CreateConverterForExtension(extension);
        if (!converter || !converter->CanImport() || !data || size == 0) return nullptr;
        return converter->ImportFromString(std::string(reinterpret_cast<const char*>(data), size),
                                           DebugLogOptions());
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
    // Opened, never previewed: PDF (see IsOpenOnlyExtension).
    provider.ImportExtensions = []() {
        return ExtensionsWhere(&IVectorFormatConverter::CanImport, true);
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
