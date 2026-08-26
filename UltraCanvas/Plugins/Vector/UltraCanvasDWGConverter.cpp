// UltraCanvas/Plugins/Vector/UltraCanvasDWGConverter.cpp
// DWG (AutoCAD Drawing) writer - see UltraCanvasCADConverters.h.
//
// DWG is a proprietary, undocumented binary format; the only open-source
// implementation that writes it is GNU LibreDWG. Rather than embed a
// reverse-engineered binary writer of uncertain fidelity, this converter
// produces the DXF writer's output and converts it with LibreDWG's
// dxf2dwg tool - the ULTRACANVAS_DXF2DWG environment variable names the
// executable, otherwise "dxf2dwg" is taken from PATH. When no tool is
// available, Export warns with that guidance and fails cleanly; the DXF
// the conversion is built on is AutoCAD's own exchange format and opens
// everywhere DWG does.
// Version: 1.0.0
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
