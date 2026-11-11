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
//
// The writers are held to the same thing, through the other locale switch. A
// stream renders its numbers through the C++ global locale, not the C one, so
// a writer leaks into its file whatever a host set with std::locale::global -
// `v 1,5 0 2`, which every other tool reads as a different vertex, or
// `element vertex 12.227` for a mesh of 12227, which no reader accepts at
// all. Each text writer's output is compared byte for byte between the two
// locales; none of them embeds a date, so the two runs agree exactly unless
// the locale leaked in.
// Version: 1.1.0 - the writers too, byte for byte between the two locales
// Version: 1.0.0
// Last Modified: 2026-11-11
// Author: UltraCanvas Framework

#include "UltraCanvasModelPreview.h"
#include "Models/UltraCanvasModelFormatsPlugin.h"
#include "Models/UltraCanvasModelMesh3D.h"

#include <algorithm>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <locale>
#include <string>
#include <vector>
#include "UltraCanvasPathUtf8.h"

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

// Both locales at once, for the length of a scope: the C one that strtod,
// atof and snprintf read, and the C++ global one that a stream copies when it
// is constructed. They are separate switches and each reaches half of the
// problem - setlocale alone leaves `stream << 1.5` writing `1.5`, which is why
// the writers below need the global one to be tested at all. A host that calls
// std::locale::global(std::locale("")) - the usual way an application honours
// the desktop's language - sets it for everything loaded into that process,
// this framework's writers included.
class InLocale {
public:
    explicit InLocale(const char* name) : saved_(SavedCLocale()) {
        std::setlocale(LC_ALL, name);
        try {
            std::locale::global(std::locale(name));
            global_ = true;
        } catch (const std::exception&) {
            global_ = false;   // the C half alone; the stream half cannot be tested
        }
    }
    ~InLocale() {
        if (global_) std::locale::global(savedGlobal_);
        std::setlocale(LC_ALL, saved_.c_str());
    }
    bool HasGlobal() const { return global_; }
    InLocale(const InLocale&) = delete;
    InLocale& operator=(const InLocale&) = delete;

private:
    static std::string SavedCLocale() {
        const char* current = std::setlocale(LC_ALL, nullptr);
        return current ? current : "C";
    }

    std::string saved_;
    std::locale savedGlobal_;
    bool global_ = false;
};

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
        if (!fs::exists(UltraCanvas::PathFromUtf8(path))) {
            std::printf("  [SKIP] %s (not in this checkout)\n", relative.c_str());
            continue;
        }
        const std::string ext = path.extension().string().substr(1);
        if (!UltraCanvasModelFormatsPlugin::CreateConverterForExtension(ext)) {
            std::printf("  [SKIP] %s (no reader in this build)\n", relative.c_str());
            continue;
        }
        Mesh3D inC, inComma;
        const bool readC = provider.Load(path.string(), inC);
        bool readComma = false;
        {
            const InLocale german(comma);
            readComma = provider.Load(path.string(), inComma);
        }
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

    // ===== THE WRITERS =====
    //
    // PreferBinary is off throughout: the binary encodings hold IEEE bytes and
    // never met the locale, and PLY's default is binary, so the text writer
    // this is here for would otherwise not run at all.
    struct WriteCase {
        const char* source;        // a document to write, read in "C"
        const char* extension;     // the writer to write it with
    };
    // .wrl is missing on purpose: the X3D converter picks its classic VRML
    // encoding from the file name, which ExportToMemory has none of, so it
    // would only exercise the XML writer a second time.
    const std::vector<WriteCase> writers = {
        {"OBJ/E-45-Aircraft.obj", "obj"},
        {"OBJ/E-45-Aircraft.obj", "ply"},
        {"OBJ/E-45-Aircraft.obj", "x3d"},
        {"OBJ/E-45-Aircraft.obj", "dae"},
        {"STEP/Box.step", "step"},   // solids, not a mesh: STEP writes exact bodies
    };
    for (const WriteCase& test : writers) {
        const fs::path path = media / test.source;
        const std::string what = std::string(test.extension) + " written from " + test.source;
        if (!fs::exists(UltraCanvas::PathFromUtf8(path))) {
            std::printf("  [SKIP] %s (source not in this checkout)\n", what.c_str());
            continue;
        }
        auto writer = UltraCanvasModelFormatsPlugin::CreateConverterForExtension(test.extension);
        if (!writer || !writer->CanExport()) {
            std::printf("  [SKIP] %s (no writer in this build)\n", what.c_str());
            continue;
        }

        ConversionOptions options;
        options.PreferBinary = false;
        auto document = UltraCanvasModelFormatsPlugin::LoadModelDocument(path.string(), options);
        if (!document) {
            std::printf("  [SKIP] %s (the source does not read in this build)\n", what.c_str());
            continue;
        }

        std::vector<uint8_t> inC, inComma;
        const bool wroteC = writer->ExportToMemory(*document, inC, options);
        bool wroteComma = false;
        {
            const InLocale german(comma);
            if (!german.HasGlobal()) {
                // Without the C++ global locale a stream still writes '.', so
                // the comparison would pass whatever the writer does.
                std::printf("  [SKIP] %s (\"%s\" is not a C++ locale here)\n",
                            what.c_str(), comma);
                continue;
            }
            wroteComma = writer->ExportToMemory(*document, inComma, options);
        }
        if (!wroteC) {
            Check(false, what + " in \"C\"");
            continue;
        }
        Check(wroteComma, what + " in \"" + comma + "\"");
        std::string why;
        if (wroteComma && inC != inComma) {
            const size_t at = static_cast<size_t>(
                    std::mismatch(inC.begin(), inC.end(), inComma.begin(), inComma.end()).first -
                    inC.begin());
            const size_t from = at > 40 ? at - 40 : 0;
            const auto piece = [&](const std::vector<uint8_t>& bytes) {
                return std::string(bytes.begin() + static_cast<long>(from),
                                   bytes.begin() + static_cast<long>(std::min(bytes.size(),
                                                                              at + 20)));
            };
            why = " - they part at byte " + std::to_string(at) + ": \"" + piece(inC) +
                  "\" against \"" + piece(inComma) + "\"";
        }
        Check(wroteComma && inC == inComma,
              what + ": the same " + std::to_string(inC.size()) + " bytes in both" + why);
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
