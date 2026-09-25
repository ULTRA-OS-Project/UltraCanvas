// Tests/ModelLocaleDecimalTest.cpp
// Every 3D sample reads the same on a comma-decimal desktop as in "C".
//
// The model formats write their numbers with a '.' by definition, but six of
// the readers parsed them with atof / strtod, which follow the desktop's
// LC_NUMERIC. On a German, French or Italian system the '.' is not a decimal
// point there, so "1.5" was read as 1: PLY, DXF 3D, COLLADA, FBX (text), STEP
// and DirectX .x models came out snapped to a whole-number grid - a plane
// became a lump of cubes - in the Filer's 3D thumbnails and detail view, and
// in every other viewer. OBJ and X3D had the same defect and were fixed in
// 0.9.42; this holds all of them to it.
//
// Each sample under media/3D is loaded through the provider
// RegisterModelFormatsPlugin installs for the previews, once
// in the "C" locale and once in a comma-decimal one, and the two meshes must
// match vertex for vertex. Where no comma-decimal locale is installed the
// comparison is reported as skipped rather than passed.
// Version: 1.0.0
// Last Modified: 2026-09-24
// Author: UltraCanvas Framework

#include "UltraCanvasModelPreview.h"
#include "Models/UltraCanvasModelFormatsPlugin.h"
#include "Models/UltraCanvasModelMesh3D.h"

#include <clocale>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace UltraCanvas;
using namespace UltraCanvas::ModelConverter;
namespace fs = std::filesystem;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

// What RegisterModelFormatsPlugin() installs, built here (see
// ModelPreviewSeamTest: the facade would pull in the UI stack).
ModelPreviewProvider RealProvider() {
    ModelPreviewProvider provider;
    provider.Extensions = [] {
        return UltraCanvasModelFormatsPlugin::SupportedLoadExtensions();
    };
    provider.Load = [](const std::string& path, Mesh3D& out) {
        ConversionOptions options;
        options.TriangulateOnImport = true;
        options.TessellateOnImport = true;
        auto document = UltraCanvasModelFormatsPlugin::LoadModelDocument(path, options);
        if (!document || document->Empty()) return false;
        out = ModelDocumentToMesh3D(*document);
        return !out.Empty();
    };
    return provider;
}

// A locale whose decimal point is a comma, or nullptr.
const char* CommaDecimalLocale() {
    for (const char* name : {"de_DE.UTF-8", "de_DE.utf8", "de_DE", "fr_FR.UTF-8",
                             "fr_FR.utf8", "German_Germany.1252", "German"}) {
        if (std::setlocale(LC_ALL, name)) {
            const bool comma = std::localeconv()->decimal_point[0] == ',';
            std::setlocale(LC_ALL, "C");
            if (comma) return name;
        }
    }
    std::setlocale(LC_ALL, "C");
    return nullptr;
}

bool SameMesh(const Mesh3D& a, const Mesh3D& b, std::string& why) {
    if (a.positions.size() != b.positions.size()) {
        why = std::to_string(a.positions.size()) + " vs " +
              std::to_string(b.positions.size()) + " vertices";
        return false;
    }
    if (a.indices != b.indices) {
        why = "different triangles";
        return false;
    }
    for (size_t i = 0; i < a.positions.size(); ++i) {
        const Vec3& p = a.positions[i];
        const Vec3& q = b.positions[i];
        if (std::fabs(p.x - q.x) > 1e-6f || std::fabs(p.y - q.y) > 1e-6f ||
            std::fabs(p.z - q.z) > 1e-6f) {
            char text[160];
            std::snprintf(text, sizeof text,   // locale-ok: a test message, read by a person
                          "vertex %zu is (%g, %g, %g) in C but (%g, %g, %g)",
                          i, p.x, p.y, p.z, q.x, q.y, q.z);
            why = text;
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const fs::path media = argc > 1 ? fs::path(argv[1]) : fs::path("media/3D");
    // Called directly rather than through LoadModelPreviewMesh: the preview
    // path gives .dxf to the Vector plugin, and the 3D DXF reader has the
    // same defect to hold to.
    const ModelPreviewProvider provider = RealProvider();

    const char* comma = CommaDecimalLocale();
    if (!comma) {
        std::printf("No comma-decimal locale installed: comparison skipped.\n");
        return 0;
    }
    std::printf("Comparing \"C\" with \"%s\"\n", comma);

    // Every text-based sample - the binary ones (3DS, MS3D, binary FBX,
    // Blend, Alembic) hold IEEE floats and never met the locale.
    const std::vector<std::string> samples = {
        "PLY/E-45-Aircraft.ply",
        "DXF/E-45-Aircraft.dxf",
        "COLLADA/E-45-Aircraft.dae",
        "FBX/E-45-Aircraft-6.1-ascii.fbx",
        "STEP/Box.step",
        "STEP/Pin.step",
        "XFile/E-45-Aircraft.x",
        "OBJ/E-45-Aircraft.obj",
        "X3D/E-45-Aircraft.x3d",
        "VRML/E-45-Aircraft.wrl",
    };
    for (const std::string& relative : samples) {
        const fs::path path = media / relative;
        if (!fs::exists(path)) {
            std::printf("  [SKIP] %s (not in this checkout)\n", relative.c_str());
            continue;
        }
        const std::string ext = path.extension().string().substr(1);
        if (!UltraCanvasModelFormatsPlugin::CreateConverterForExtension(ext)) {
            std::printf("  [SKIP] %s (no reader in this build)\n", relative.c_str());
            continue;
        }
        Mesh3D inC, inComma;
        std::setlocale(LC_ALL, "C");
        const bool readC = provider.Load(path.string(), inC);
        std::setlocale(LC_ALL, comma);
        const bool readComma = provider.Load(path.string(), inComma);
        std::setlocale(LC_ALL, "C");
        if (!readC) {
            Check(false, relative + " reads in \"C\"");
            continue;
        }
        Check(readComma, relative + " reads in \"" + comma + "\"");
        std::string why;
        Check(readComma && SameMesh(inC, inComma, why),
              relative + ": the same " + std::to_string(inC.positions.size()) +
              " vertices in both" + (why.empty() ? "" : " - " + why));
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
