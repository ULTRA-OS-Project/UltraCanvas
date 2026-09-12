// core/UltraCanvasLaTeXModuleLoader.cpp
// On-demand loader for the LaTeX math module (libUltraCanvasLaTeX).
//
// The core does NOT link the LaTeX module or the MicroTeX engine. The first
// call to CreateLaTeXView() dlopen()s the module, resolves its C ABI (see
// UltraCanvasLaTeXModuleABI.h), and hands back a view whose concrete type lives
// in the module. The module stays mapped for the process lifetime (or until
// UnloadLaTeXModule()); allocation/deallocation of views always happen inside
// the module via the matching create/destroy entry points.
//
// Version: 1.2.0
// Last Modified: 2026-09-12
// Author: UltraCanvas Framework

#ifdef ULTRACANVAS_PLUGIN_LATEX

#include "Plugins/LaTeX/UltraCanvasLaTeXView.h"
#include "Plugins/LaTeX/UltraCanvasLaTeXModuleABI.h"
#include "UltraCanvasUtils.h"   // GetExecutableDir

#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <dlfcn.h>
#endif

namespace UltraCanvas {

namespace {

struct Module {
    void* handle = nullptr;
    UltraCanvasLaTeXModule_CreateViewFn       create   = nullptr;
    UltraCanvasLaTeXModule_DestroyViewFn      destroy  = nullptr;
    UltraCanvasLaTeXModule_SetFontSearchDirFn setFont  = nullptr;
    UltraCanvasLaTeXModule_TypesetInlineFn typesetInline = nullptr;
    UltraCanvasLaTeXModule_InlineMetricsFn inlineMetrics = nullptr;
    UltraCanvasLaTeXModule_DrawInlineFn    drawInline    = nullptr;
    UltraCanvasLaTeXModule_ReleaseInlineFn releaseInline = nullptr;
};

std::once_flag g_loadOnce;
Module         g_module;
std::string    g_error;
std::string    g_modulePathOverride;  // explicit file or directory
std::string    g_fontDir;             // forwarded to the module after load

// --- platform shims ---------------------------------------------------------

// File names the module may carry on this platform, most likely first.
std::vector<std::string> ModuleFileNames() {
#if defined(_WIN32)
    // CMake names the MODULE "UltraCanvasLaTeX.dll" (PREFIX ""); a MinGW build
    // tree from before that setting carries the toolchain's "lib" prefix.
    return {"UltraCanvasLaTeX.dll", "libUltraCanvasLaTeX.dll"};
#elif defined(__APPLE__)
    return {"libUltraCanvasLaTeX.dylib"};
#else
    return {"libUltraCanvasLaTeX.so"};
#endif
}

std::string ModuleFileName() { return ModuleFileNames().front(); }

#if defined(_WIN32)
bool IsAbsoluteWindowsPath(const std::string& p) {
    return (p.size() > 2 && p[1] == ':') ||
           (p.size() > 2 && (p[0] == '\\' || p[0] == '/') && (p[1] == '\\' || p[1] == '/'));
}
#endif

void* OpenLib(const std::string& path) {
#if defined(_WIN32)
    // With an absolute path, let the module's own directory lead the search
    // for its dependencies (the package ships it in lib/ next to the
    // executable, with the core DLL and the runtime beside the .exe). The
    // flag's behaviour is undefined for a relative path, so the bare-name
    // probe goes through plain LoadLibrary.
    HMODULE h = IsAbsoluteWindowsPath(path)
        ? LoadLibraryExA(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH)
        : LoadLibraryA(path.c_str());
    return reinterpret_cast<void*>(h);
#else
    // RTLD_GLOBAL so the module can resolve UltraCanvas core symbols from the
    // already-loaded host image (works whether core is shared or statically
    // linked into an executable built with exported dynamic symbols).
    return dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
#endif
}

void* Sym(void* h, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(h), name));
#else
    return dlsym(h, name);
#endif
}

void CloseLib(void* h) {
#if defined(_WIN32)
    if (h) FreeLibrary(reinterpret_cast<HMODULE>(h));
#else
    if (h) dlclose(h);
#endif
}

std::string LastDlError() {
#if defined(_WIN32)
    const DWORD code = GetLastError();
    std::string msg = "LoadLibrary failed (code " + std::to_string(code) + ")";
    // The two codes a deployment problem produces, spelled out so the message
    // in the UI says what to check instead of just a number.
    if (code == ERROR_MOD_NOT_FOUND)       msg += " - the file, or a DLL it depends on, was not found";
    else if (code == ERROR_BAD_EXE_FORMAT) msg += " - not a valid image for this architecture";
    return msg;
#else
    const char* e = dlerror();
    return e ? std::string(e) : std::string("unknown dynamic-link error");
#endif
}

bool LooksLikeFile(const std::string& p) {
    // A crude check: treat as a file path if it ends in a known suffix.
    return p.size() > 3 &&
           (p.rfind(".so") != std::string::npos ||
            p.rfind(".dylib") != std::string::npos ||
            p.rfind(".dll") != std::string::npos);
}

std::vector<std::string> CandidatePaths() {
    std::vector<std::string> out;
    const std::vector<std::string> names = ModuleFileNames();

    // Directories to probe, in priority order; each is tried with every
    // file name the module may carry.
    std::vector<std::string> dirs;
    if (!g_modulePathOverride.empty()) {
        if (LooksLikeFile(g_modulePathOverride)) out.push_back(g_modulePathOverride);
        else dirs.push_back(g_modulePathOverride);
    }
    if (const char* env = std::getenv("ULTRACANVAS_PLUGIN_DIR"); env && *env) {
        dirs.emplace_back(env);
    }
    const std::string exe = GetExecutableDir();
    if (!exe.empty()) {
        dirs.push_back(exe);
        dirs.push_back(exe + "/plugins");
        // Dev build: the apps are emitted at the build root while the module
        // (a CMake MODULE) goes to <build>/lib. With a static core there is
        // no rpath for the bare-name dlopen below to fall back on, so this
        // layout has to be probed explicitly. The Windows package ships the
        // module in lib/ next to the executable for the same reason.
        dirs.push_back(exe + "/lib");
        // Package layout: executable in bin/, module in lib/.
        dirs.push_back(exe + "/../lib");
        dirs.push_back(exe + "/../lib/ultracanvas");
#if defined(__APPLE__)
        // App bundle: Contents/MacOS/<exe>, module in Contents/PlugIns/.
        dirs.push_back(exe + "/../PlugIns");
#endif
    }
    for (const auto& d : dirs)
        for (const auto& n : names) out.push_back(d + "/" + n);
    // Finally let the dynamic linker search its own paths (rpath/LD_LIBRARY_PATH).
    for (const auto& n : names) out.push_back(n);
    return out;
}

void LoadOnce() {
    g_error.clear();
    std::string tried;
    for (const auto& path : CandidatePaths()) {
        void* h = OpenLib(path);
        if (!h) { tried += "\n  " + path + " : " + LastDlError(); continue; }

        auto abiFn = reinterpret_cast<UltraCanvasLaTeXModule_ABIVersionFn>(
            Sym(h, ULTRACANVAS_LATEX_SYM_ABI_VERSION));
        auto createFn = reinterpret_cast<UltraCanvasLaTeXModule_CreateViewFn>(
            Sym(h, ULTRACANVAS_LATEX_SYM_CREATE_VIEW));
        auto destroyFn = reinterpret_cast<UltraCanvasLaTeXModule_DestroyViewFn>(
            Sym(h, ULTRACANVAS_LATEX_SYM_DESTROY_VIEW));

        if (!abiFn || !createFn || !destroyFn) {
            g_error = "LaTeX module '" + path + "' is missing required entry points";
            CloseLib(h);
            return;
        }
        if (abiFn() != ULTRACANVAS_LATEX_ABI_VERSION) {
            g_error = "LaTeX module '" + path + "' ABI version " +
                      std::to_string(abiFn()) + " != expected " +
                      std::to_string(ULTRACANVAS_LATEX_ABI_VERSION);
            CloseLib(h);
            return;
        }

        g_module.handle  = h;
        g_module.create  = createFn;
        g_module.destroy = destroyFn;
        g_module.setFont = reinterpret_cast<UltraCanvasLaTeXModule_SetFontSearchDirFn>(
            Sym(h, ULTRACANVAS_LATEX_SYM_SET_FONT_DIR));
        g_module.typesetInline = reinterpret_cast<UltraCanvasLaTeXModule_TypesetInlineFn>(
            Sym(h, ULTRACANVAS_LATEX_SYM_TYPESET_INLINE));
        g_module.inlineMetrics = reinterpret_cast<UltraCanvasLaTeXModule_InlineMetricsFn>(
            Sym(h, ULTRACANVAS_LATEX_SYM_INLINE_METRICS));
        g_module.drawInline = reinterpret_cast<UltraCanvasLaTeXModule_DrawInlineFn>(
            Sym(h, ULTRACANVAS_LATEX_SYM_DRAW_INLINE));
        g_module.releaseInline = reinterpret_cast<UltraCanvasLaTeXModule_ReleaseInlineFn>(
            Sym(h, ULTRACANVAS_LATEX_SYM_RELEASE_INLINE));

        if (g_module.setFont && !g_fontDir.empty()) g_module.setFont(g_fontDir.c_str());
        return;
    }
    g_error = "LaTeX module (" + ModuleFileName() + ") not found. Tried:" + tried;
}

} // namespace

// ===== public API =====

void SetLaTeXModulePath(const std::string& pathOrDir) { g_modulePathOverride = pathOrDir; }

void SetLaTeXFontSearchDir(const std::string& dir) {
    g_fontDir = dir;
    // If the module is already loaded, forward immediately.
    if (g_module.setFont) g_module.setFont(g_fontDir.c_str());
}

bool IsLaTeXModuleAvailable() {
    std::call_once(g_loadOnce, LoadOnce);
    return g_module.create != nullptr;
}

const std::string& GetLaTeXModuleError() { return g_error; }

std::shared_ptr<UltraCanvasLaTeXView> CreateLaTeXView(
        const std::string& identifier, float x, float y, float w, float h) {
    if (!IsLaTeXModuleAvailable()) return nullptr;

    UltraCanvasLaTeXView* raw = g_module.create(identifier.c_str(), x, y, w, h);
    if (!raw) return nullptr;

    // Free through the module's own destroyer so allocation and deallocation
    // stay on the same side of the dynamic boundary.
    auto destroy = g_module.destroy;
    return std::shared_ptr<UltraCanvasLaTeXView>(raw, [destroy](UltraCanvasLaTeXView* p) {
        if (p && destroy) destroy(p);
    });
}

// ----- inline math entry points (used by core/UltraCanvasInlineMath.cpp) -----

struct LaTeXInlineEntryPoints {
    UltraCanvasLaTeXModule_TypesetInlineFn typeset = nullptr;
    UltraCanvasLaTeXModule_InlineMetricsFn metrics = nullptr;
    UltraCanvasLaTeXModule_DrawInlineFn draw = nullptr;
    UltraCanvasLaTeXModule_ReleaseInlineFn release = nullptr;
};

bool GetLaTeXInlineEntryPoints(LaTeXInlineEntryPoints& out) {
    if (!IsLaTeXModuleAvailable()) return false;
    if (!g_module.typesetInline || !g_module.inlineMetrics || !g_module.drawInline || !g_module.releaseInline) return false;
    out.typeset = g_module.typesetInline;
    out.metrics = g_module.inlineMetrics;
    out.draw = g_module.drawInline;
    out.release = g_module.releaseInline;
    return true;
}

void UnloadLaTeXModule() {
    if (g_module.handle) CloseLib(g_module.handle);
    g_module = Module{};
    // Note: g_loadOnce cannot be reset; a fresh load after unload would require
    // a process restart. UnloadLaTeXModule is intended for shutdown only.
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_PLUGIN_LATEX
