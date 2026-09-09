// UltraCanvas/Plugins/Vector/UltraCanvasVectorFormatsPlugin.cpp
// Implementation of the vector formats graphics plugin - see the header.
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "UltraCanvasVectorFormatsPlugin.h"

#include "UltraCanvasCADConverters.h"
#include "UltraCanvasCDRConverter.h"
#include "UltraCanvasEPSConverter.h"
#include "UltraCanvasMetafileConverters.h"
#include "UltraCanvasXARConverter.h"

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

std::unique_ptr<IVectorFormatConverter>
UltraCanvasVectorFormatsPlugin::CreateConverterForExtension(
        const std::string& extensionOrPath) {
    std::string ext = ExtensionOf(extensionOrPath);
    if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
    if (ext == "svg" || ext == "svgz") return std::make_unique<SVGConverter>();
    if (ext == "xar") return std::make_unique<XARConverter>();
    if (ext == "eps" || ext == "epsf" || ext == "ps")
        return std::make_unique<EPSConverter>();
    if (ext == "cdr") return std::make_unique<CDRConverter>();
    if (ext == "pdf") return std::make_unique<PDFVectorConverter>();
    if (ext == "emf") return std::make_unique<EMFConverter>();
    if (ext == "wmf") return std::make_unique<WMFConverter>();
    if (ext == "ai") return std::make_unique<AIConverter>();
    if (ext == "dxf") return std::make_unique<DXFConverter>();
    if (ext == "dwg") return std::make_unique<DWGConverter>();
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
    return CanHandle("." + fileInfo.extension);
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

bool UltraCanvasVectorFormatsPlugin::SaveGraphics(
        const std::shared_ptr<UltraCanvasUIElement>& element,
        const std::string& filePath) {
    auto vectorElement = std::dynamic_pointer_cast<UltraCanvasVectorElement>(element);
    if (!vectorElement || !vectorElement->HasDocument()) return false;
    return SaveVectorDocument(*vectorElement->GetDocument(), filePath);
}

} // namespace UltraCanvas
