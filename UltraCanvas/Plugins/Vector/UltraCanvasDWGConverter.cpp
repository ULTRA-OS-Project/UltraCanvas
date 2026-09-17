// UltraCanvas/Plugins/Vector/UltraCanvasDWGConverter.cpp
// DWG (AutoCAD Drawing) converter - see UltraCanvasCADConverters.h.
//
// Reading is native: UltraCanvasDWGDecoder.cpp decodes the R13-R2018
// drawing database (bit-coded objects, compressed R2004+ pages, Reed-
// Solomon R2007 pages) and renders it as tagged DXF, which the DXF reader
// turns into the document - so a .dwg previews and opens without any
// external program. GNU LibreDWG's dwg2dxf, when installed, is only tried
// for files the native decoder declines (pre-R13 drawings, damaged files).
//
// A drawing is not always named .dwg: AutoCAD writes the same database to
// .dwt (template), .dws (drawing standards) and .sv$ (automatic save), and
// copies it verbatim to .bak. IsDrawingExtension() names the first three
// alongside .dwg for callers that dispatch on the suffix;
// IsAmbiguousDrawingExtension() marks .bak, whose suffix half the world
// uses, as one to confirm with ValidateFile() before claiming it.
//
// Writing still delegates to LibreDWG's dxf2dwg (ULTRACANVAS_DXF2DWG names
// the executable, otherwise PATH): DWG is a proprietary format with no
// public specification and the framework is MIT-licensed, so the GPL
// implementation stays an optional external process. Without the tool the
// export warns with that guidance and fails cleanly; the DXF the export is
// built on is AutoCAD's own exchange format and opens everywhere DWG does.
// Version: 1.3.0
// Last Modified: 2026-09-16
// Author: UltraCanvas Framework

#include "UltraCanvasCADConverters.h"
#include "UltraCanvasDWGDecoder.h"
#include "DataFormats/UltraCanvasVectorStorage.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace UltraCanvas {
namespace VectorConverter {

FormatCapabilities DWGConverter::GetCapabilities() const {
    // The content is the DXF writer's; capabilities are identical.
    return DXFConverter().GetCapabilities();
}

std::string DWGConverter::FindDxf2Dwg() {
    if (const char* env = std::getenv("ULTRACANVAS_DXF2DWG")) {
        if (*env && std::ifstream(env).good()) return env;
    }
    if (std::system("command -v dxf2dwg >/dev/null 2>&1") == 0) return "dxf2dwg";
    return {};
}

std::string DWGConverter::FindDwg2Dxf() {
    if (const char* env = std::getenv("ULTRACANVAS_DWG2DXF")) {
        if (*env && std::ifstream(env).good()) return env;
    }
    if (std::system("command -v dwg2dxf >/dev/null 2>&1") == 0) return "dwg2dxf";
    return {};
}

std::string DWGConverter::DecodeToDxf(const std::string& data,
                                      const ConversionOptions& options) {
    DWGDecodeResult res = DecodeDWG(data, options.WarningCallback);
    if (!res.ok) {
        if (options.WarningCallback) {
            options.WarningCallback("DWG import: " + res.error);
        }
        return {};
    }
    return res.dxf;
}

std::shared_ptr<VectorStorage::VectorDocument> DWGConverter::ImportFromString(
        const std::string& data, const ConversionOptions& options) {
    if (!ValidateData(data)) {
        if (options.WarningCallback) {
            options.WarningCallback("Not a DWG file (missing AC10xx magic)");
        }
        return nullptr;
    }

    // Native decoder first.
    if (DWGDecoderSupportsVersion(data)) {
        std::string dxf = DecodeToDxf(data, options);
        if (!dxf.empty()) {
            auto doc = DXFConverter().ImportFromString(dxf, options);
            if (doc) return doc;
        }
    } else if (options.WarningCallback) {
        options.WarningCallback("DWG import: " + data.substr(0, 6) +
                                " (pre-R13) drawings are not decoded natively");
    }

    // Fallback: LibreDWG's dwg2dxf when it is installed.
    std::string tool = FindDwg2Dxf();
    if (tool.empty()) {
        if (options.WarningCallback) {
            options.WarningCallback(
                    "DWG import failed; LibreDWG's dwg2dxf (ULTRACANVAS_DWG2DXF "
                    "or PATH) would be tried as a fallback when installed");
        }
        return nullptr;
    }

    // dwg2dxf works on files; stage through temporaries.
    std::string base = std::string("/tmp/uc_dwg_in_") + std::to_string(
            reinterpret_cast<uintptr_t>(&data) ^
            static_cast<uintptr_t>(data.size()));
    std::string dwgPath = base + ".dwg";
    std::string dxfPath = base + ".dxf";
    {
        std::ofstream f(dwgPath, std::ios::binary);
        if (!f.is_open()) return nullptr;
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
    }
    std::string cmd = tool + " -y -o " + dxfPath + " " + dwgPath + " >/dev/null 2>&1";
    int rc = std::system(cmd.c_str());
    std::remove(dwgPath.c_str());
    std::string dxfData;
    {
        std::ifstream f(dxfPath, std::ios::binary);
        if (f.is_open()) {
            std::ostringstream ss;
            ss << f.rdbuf();
            dxfData = ss.str();
        }
    }
    std::remove(dxfPath.c_str());
    if (rc != 0 || dxfData.empty()) {
        if (options.WarningCallback) {
            options.WarningCallback("dwg2dxf failed to convert the drawing");
        }
        return nullptr;
    }
    return DXFConverter().ImportFromString(dxfData, options);
}

std::string DWGConverter::ExportToString(
        const VectorStorage::VectorDocument& document,
        const ConversionOptions& options) {
    std::string tool = FindDxf2Dwg();
    if (tool.empty()) {
        if (options.WarningCallback) {
            options.WarningCallback(
                    "DWG export needs LibreDWG's dxf2dwg tool (set "
                    "ULTRACANVAS_DXF2DWG or put dxf2dwg on PATH); DWG is a "
                    "proprietary format with no public specification. "
                    "Export DXF instead - AutoCAD's own exchange format.");
        }
        return {};
    }

    DXFConverter dxf;
    std::string dxfData = dxf.ExportToString(document, options);
    if (dxfData.empty()) return {};

    // dxf2dwg works on files; stage through temporaries.
    std::string base = std::string("/tmp/uc_dwg_") + std::to_string(
            reinterpret_cast<uintptr_t>(&document) ^
            static_cast<uintptr_t>(dxfData.size()));
    std::string dxfPath = base + ".dxf";
    std::string dwgPath = base + ".dwg";
    {
        std::ofstream f(dxfPath, std::ios::binary);
        if (!f.is_open()) return {};
        f.write(dxfData.data(), static_cast<std::streamsize>(dxfData.size()));
    }
    std::string cmd = tool + " -y -o " + dwgPath + " " + dxfPath + " >/dev/null 2>&1";
    int rc = std::system(cmd.c_str());
    std::remove(dxfPath.c_str());
    std::string result;
    {
        std::ifstream f(dwgPath, std::ios::binary);
        if (f.is_open()) {
            std::ostringstream ss;
            ss << f.rdbuf();
            result = ss.str();
        }
    }
    std::remove(dwgPath.c_str());
    if (rc != 0 || result.empty() || !ValidateData(result)) {
        if (options.WarningCallback) {
            options.WarningCallback("dxf2dwg failed to convert the drawing");
        }
        return {};
    }
    return result;
}

bool DWGConverter::ValidateFile(const std::string& filename) const {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;
    std::string head(6, '\0');
    file.read(head.data(), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<size_t>(file.gcount()));
    return ValidateData(head);
}

bool DWGConverter::ValidateData(const std::string& data) const {
    // Every DWG starts with its version string: "AC10xx" / "AC1015" etc.
    return data.size() >= 6 && data.compare(0, 4, "AC10") == 0 &&
           std::isdigit(static_cast<unsigned char>(data[4])) &&
           std::isdigit(static_cast<unsigned char>(data[5]));
}

namespace {

// The lowercase tail after the last dot of an extension or a path, without
// the dot. "Plan.DWT" and ".dwt" and "dwt" all give "dwt"; a name with no
// dot is taken to be the extension itself.
std::string LowerExtensionOf(const std::string& extensionOrPath) {
    size_t dot = extensionOrPath.find_last_of('.');
    std::string ext = dot == std::string::npos ? extensionOrPath
                                               : extensionOrPath.substr(dot + 1);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

}   // namespace

bool DWGConverter::IsDrawingExtension(const std::string& extensionOrPath) {
    // A drawing, a template, a standards file and an automatic save are one
    // format under four names - AutoCAD writes the same drawing database to
    // all of them, so what opens a .dwg opens these unchanged.
    const std::string ext = LowerExtensionOf(extensionOrPath);
    return ext == "dwg" || ext == "dwt" || ext == "dws" || ext == "sv$";
}

bool DWGConverter::IsAmbiguousDrawingExtension(const std::string& extensionOrPath) {
    return LowerExtensionOf(extensionOrPath) == "bak";
}

} // namespace VectorConverter
} // namespace UltraCanvas
