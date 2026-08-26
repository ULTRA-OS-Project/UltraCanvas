// UltraCanvas/Plugins/Vector/UltraCanvasDWGConverter.cpp
// DWG (AutoCAD Drawing) converter - see UltraCanvasCADConverters.h.
//
// DWG is a proprietary, undocumented binary format; the only open-source
// implementation is GNU LibreDWG. Rather than embed a reverse-engineered
// binary codec of uncertain fidelity, this converter delegates both
// directions to LibreDWG's command-line tools: writing produces the DXF
// writer's output and converts it with dxf2dwg (ULTRACANVAS_DXF2DWG names
// the executable, otherwise PATH), reading converts with dwg2dxf
// (ULTRACANVAS_DWG2DXF, otherwise PATH) and parses the result with the
// DXF reader. When no tool is available the converter warns with that
// guidance and fails cleanly; the DXF the conversion is built on is
// AutoCAD's own exchange format and opens everywhere DWG does.
// Version: 1.1.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "UltraCanvasCADConverters.h"
#include "UltraCanvasVectorStorage.h"

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

std::shared_ptr<VectorStorage::VectorDocument> DWGConverter::ImportFromString(
        const std::string& data, const ConversionOptions& options) {
    if (!ValidateData(data)) {
        if (options.WarningCallback) {
            options.WarningCallback("Not a DWG file (missing AC10xx magic)");
        }
        return nullptr;
    }
    std::string tool = FindDwg2Dxf();
    if (tool.empty()) {
        if (options.WarningCallback) {
            options.WarningCallback(
                    "DWG import needs LibreDWG's dwg2dxf tool (set "
                    "ULTRACANVAS_DWG2DXF or put dwg2dxf on PATH); DWG is a "
                    "proprietary format with no public specification.");
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

} // namespace VectorConverter
} // namespace UltraCanvas
