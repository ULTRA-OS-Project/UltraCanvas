// Apps/UltraCanvasStart/engine/StartPackages.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "StartPackages.h"
#include "StartSystem.h"

#include <algorithm>

namespace UltraCanvasStart {

namespace {

// MSYS2 names are written for CLANG64 here and rewritten by architecture.
constexpr const char* kM = "mingw-w64-clang-x86_64-";

std::string M(const char* name) { return std::string(kM) + name; }

std::vector<Dependency> BuildTable() {
    using G = DependencyGroup;
    using K = CheckKind;
    std::vector<Dependency> t;
    auto add = [&](Dependency d) { t.push_back(std::move(d)); };

    // ---- Toolchain ----------------------------------------------------------
    add({ "compiler", "C++20 compiler (clang or GCC 11+)", G::Toolchain, K::Tool, "c++", "",
          "clang", "gcc-c++", "clang", "gcc-c++", "", M("clang") });
    add({ "build-essential", "make and the C library headers", G::Toolchain, K::Tool, "make", "",
          "build-essential", "make", "base-devel", "make", "", "" });
    add({ "cmake", "CMake 3.16 or newer", G::Toolchain, K::Tool, "cmake", "3.16",
          "cmake", "cmake", "cmake", "cmake", "cmake", M("cmake") });
    add({ "ninja", "Ninja (the generator the Windows build uses)", G::Toolchain, K::Tool, "ninja", "",
          "", "", "", "", "", M("ninja") });
    add({ "pkg-config", "pkg-config", G::Toolchain, K::Tool, "pkg-config", "",
          "pkg-config", "pkgconf-pkg-config", "pkgconf", "pkg-config", "pkg-config", M("pkgconf") });
    add({ "git", "git", G::Toolchain, K::Tool, "git", "",
          "git", "git", "git", "git", "git", "git" });
    add({ "cppwinrt", "C++/WinRT projection headers", G::Toolchain, K::None, "", "",
          "", "", "", "", "", M("cppwinrt") });
    add({ "zip", "zip (packaging on Windows)", G::Toolchain, K::Tool, "zip", "",
          "", "", "", "", "", "zip" });

    // ---- Core -------------------------------------------------------------
    add({ "cairo", "Cairo 2D rendering", G::Core, K::PkgConfig, "cairo", "",
          "libcairo2-dev", "cairo-devel", "cairo", "cairo-devel", "cairo", M("cairo") });
    add({ "pango", "Pango text layout", G::Core, K::PkgConfig, "pango", "",
          "libpango1.0-dev", "pango-devel", "pango", "pango-devel", "pango", M("pango") });
    add({ "harfbuzz", "HarfBuzz text shaping", G::Core, K::PkgConfig, "harfbuzz", "",
          "libharfbuzz-dev", "harfbuzz-devel", "harfbuzz", "harfbuzz-devel", "harfbuzz", M("harfbuzz") });
    add({ "freetype", "FreeType fonts", G::Core, K::PkgConfig, "freetype2", "",
          "libfreetype6-dev", "freetype-devel", "freetype2", "freetype2-devel", "freetype", M("freetype") });
    add({ "glib", "GLib", G::Core, K::PkgConfig, "glib-2.0", "",
          "libglib2.0-dev", "glib2-devel", "glib2", "glib2-devel", "glib", M("glib2") });
    add({ "vips", "libvips image processing", G::Core, K::PkgConfig, "vips-cpp", "",
          "libvips-dev", "vips-devel", "libvips", "vips-devel", "vips", M("libvips") });
    add({ "tinyxml2", "tinyxml2 XML", G::Core, K::PkgConfig, "tinyxml2", "",
          "libtinyxml2-dev", "tinyxml2-devel", "tinyxml2", "tinyxml2-devel", "tinyxml2", M("tinyxml2") });
    // fmt: CMake fetches it when the system has none, which is how the
    // macOS CI leg builds; Homebrew's is therefore not required there.
    add({ "fmt", "fmt formatting (fetched by CMake when absent)", G::Core, K::PkgConfig, "fmt", "",
          "libfmt-dev", "fmt-devel", "fmt", "fmt-devel", "", M("fmt") });
    add({ "x11", "X11 (window system)", G::Core, K::PkgConfig, "x11", "",
          "libx11-dev", "libX11-devel", "libx11", "libX11-devel", "", "" });
    add({ "xcursor", "Xcursor", G::Core, K::PkgConfig, "xcursor", "",
          "libxcursor-dev", "libXcursor-devel", "libxcursor", "libXcursor-devel", "", "" });
    add({ "gl", "OpenGL headers", G::Core, K::PkgConfig, "gl", "",
          "libgl1-mesa-dev", "mesa-libGL-devel", "mesa", "Mesa-libGL-devel", "", "" });
    add({ "gtk3", "GTK 3 (native file dialogs)", G::Core, K::PkgConfig, "gtk+-3.0", "",
          "libgtk-3-dev", "gtk3-devel", "gtk3", "gtk3-devel", "", "" });
    add({ "iconv", "libiconv", G::Core, K::None, "", "",
          "", "", "", "", "", M("libiconv") });
    add({ "zlib", "zlib", G::Core, K::PkgConfig, "zlib", "",
          "zlib1g-dev", "zlib-devel", "zlib", "zlib-devel", "", M("zlib") });
    add({ "glew", "GLEW (OpenGL loader on Windows)", G::Core, K::None, "", "",
          "", "", "", "", "", M("glew") });
    // libcurl: macOS uses Apple's system libcurl, which has no .pc file, so
    // no Homebrew package is named and nothing is checked there.
    add({ "curl", "libcurl (UltraNet)", G::Core, K::PkgConfig, "libcurl", "",
          "libcurl4-openssl-dev", "libcurl-devel", "curl", "libcurl-devel", "", M("curl-winssl") });

    // ---- CDR plug-in --------------------------------------------------------
    add({ "libcdr", "libcdr (CorelDRAW)", G::Cdr, K::PkgConfig, "libcdr-0.1", "",
          "libcdr-dev", "libcdr-devel", "libcdr", "libcdr-devel", "libcdr", M("libcdr") });
    add({ "librevenge", "librevenge", G::Cdr, K::PkgConfig, "librevenge-0.0", "",
          "librevenge-dev", "librevenge-devel", "librevenge", "librevenge-devel", "librevenge", M("librevenge") });
    add({ "boost", "Boost headers", G::Cdr, K::None, "", "",
          "libboost-dev", "boost-devel", "boost", "boost-devel", "boost", M("boost") });
    add({ "lcms2", "Little CMS 2", G::Cdr, K::PkgConfig, "lcms2", "",
          "liblcms2-dev", "lcms2-devel", "lcms2", "liblcms2-devel", "little-cms2", M("lcms2") });
    add({ "icu", "ICU", G::Cdr, K::PkgConfig, "icu-uc", "",
          "libicu-dev", "libicu-devel", "icu", "libicu-devel", "icu4c", M("icu") });

    // ---- PDF ----------------------------------------------------------------
    add({ "mupdf", "MuPDF", G::Pdf, K::None, "", "",
          "libmupdf-dev", "mupdf-devel", "libmupdf", "mupdf-devel", "mupdf", M("mupdf") });

    // ---- OCR ----------------------------------------------------------------
    add({ "tesseract", "Tesseract OCR", G::Ocr, K::PkgConfig, "tesseract", "",
          "libtesseract-dev", "tesseract-devel", "tesseract", "tesseract-ocr-devel", "tesseract", M("tesseract-ocr") });
    add({ "leptonica", "Leptonica", G::Ocr, K::PkgConfig, "lept", "",
          "libleptonica-dev", "leptonica-devel", "leptonica", "leptonica-devel", "leptonica", M("leptonica") });

    // ---- Vectorizer ---------------------------------------------------------
    add({ "rust", "Rust toolchain (cargo)", G::Vectorizer, K::Tool, "cargo", "",
          "cargo", "cargo", "rust", "cargo", "rust", M("rust") });

    // ---- Audio --------------------------------------------------------------
    add({ "flac", "FLAC", G::Audio, K::PkgConfig, "flac", "",
          "libflac-dev", "flac-devel", "flac", "flac-devel", "flac", M("flac") });
    add({ "vorbis", "Vorbis", G::Audio, K::PkgConfig, "vorbisfile", "",
          "libvorbis-dev", "libvorbis-devel", "libvorbis", "libvorbis-devel", "libvorbis", M("libvorbis") });
    add({ "opus", "Opus", G::Audio, K::PkgConfig, "opus", "",
          "libopus-dev", "opus-devel", "opus", "libopus-devel", "opus", M("opus") });
    add({ "opusfile", "opusfile", G::Audio, K::PkgConfig, "opusfile", "",
          "libopusfile-dev", "opusfile-devel", "opusfile", "opusfile-devel", "opusfile", M("opusfile") });
    add({ "lame", "LAME MP3 encoder", G::Audio, K::None, "", "",
          "libmp3lame-dev", "lame-devel", "lame", "libmp3lame-devel", "lame", M("lame") });

    // ---- Barcode ------------------------------------------------------------
    add({ "zbar", "zbar", G::Barcode, K::PkgConfig, "zbar", "",
          "libzbar-dev", "zbar-devel", "zbar", "zbar-devel", "zbar", M("zbar") });

    // ---- Net / crypt extras -------------------------------------------------
    add({ "c-ares", "c-ares (asynchronous DNS)", G::Net, K::PkgConfig, "libcares", "",
          "libc-ares-dev", "c-ares-devel", "c-ares", "c-ares-devel", "c-ares", M("c-ares") });
    add({ "libsodium", "libsodium", G::Net, K::PkgConfig, "libsodium", "",
          "libsodium-dev", "libsodium-devel", "libsodium", "libsodium-devel", "libsodium", M("libsodium") });
    return t;
}

} // namespace

const std::vector<Dependency>& AllDependencies() {
    static const std::vector<Dependency> table = BuildTable();
    return table;
}

std::vector<const Dependency*> DependenciesFor(const Choices& choices,
                                               PackageManager manager) {
    std::vector<const Dependency*> result;
    for (const auto& dependency : AllDependencies()) {
        if (!choices.Has(dependency.group)) continue;
        if (manager != PackageManager::None && dependency.PackageFor(manager).empty()) continue;
        result.push_back(&dependency);
    }
    return result;
}

std::vector<std::string> PackageNames(const std::vector<const Dependency*>& dependencies,
                                      PackageManager manager) {
    std::vector<std::string> names;
    for (const auto* dependency : dependencies) {
        const std::string& name = dependency->PackageFor(manager);
        if (name.empty()) continue;
        if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
    }
    return names;
}

PlanStep InstallStep(PackageManager manager, const std::vector<std::string>& packages,
                     const std::string& program) {
    PlanStep step;
    step.kind = StepKind::Install;
    step.title = "Install " + std::to_string(packages.size()) + " package" +
                 (packages.size() == 1 ? "" : "s") + " with " + PackageManagerName(manager);
    const std::string exe = program.empty() ? PackageManagerProgram(manager) : program;
    switch (manager) {
        case PackageManager::Apt:
            step.argv = { exe, "install", "-y" };
            step.needsElevation = true;
            break;
        case PackageManager::Dnf:
            step.argv = { exe, "install", "-y" };
            step.needsElevation = true;
            break;
        case PackageManager::Pacman:
            step.argv = { exe, "-S", "--needed", "--noconfirm" };
            step.needsElevation = true;
            break;
        case PackageManager::Msys2Pacman:
            step.argv = { exe, "-S", "--needed", "--noconfirm" };
            step.needsElevation = false;
            break;
        case PackageManager::Zypper:
            step.argv = { exe, "install", "-y" };
            step.needsElevation = true;
            break;
        case PackageManager::Homebrew:
            step.argv = { exe, "install" };
            step.needsElevation = false;
            break;
        default:
            step.kind = StepKind::Manual;
            step.title = "Install the development packages by hand";
            step.description = "No package manager was recognised on this system.";
            return step;
    }
    step.argv.insert(step.argv.end(), packages.begin(), packages.end());
    std::string list;
    for (const auto& p : packages) list += (list.empty() ? "" : " ") + p;
    step.description = "Installs the development packages the chosen features need: " + list;
    return step;
}

std::string Msys2PackagePrefix(const std::string& architecture) {
    return architecture == "arm64" ? "mingw-w64-clang-aarch64-" : "mingw-w64-clang-x86_64-";
}

std::string Msys2PackageForArchitecture(const std::string& package,
                                        const std::string& architecture) {
    const std::string from = kM;
    if (package.compare(0, from.size(), from) != 0) return package;
    return Msys2PackagePrefix(architecture) + package.substr(from.size());
}

} // namespace UltraCanvasStart
