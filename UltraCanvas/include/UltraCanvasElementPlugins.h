// include/UltraCanvasElementPlugins.h
// Framework-wide element plugin system: descriptors, registry, host ABI and
// the plugin loader API. Implements UltraCanvasElementPluginSystemProposal.md
// (P1 registry + P2 loader core; manifest-indexed lazy loading follows later).
//
// Every pluggable thing - chart, diagram, widget, tool - is described by a
// UCElementDescriptor registered under a stable kebab-case type name, whether
// it is compiled into the application or loaded from a DSO. Creation by name
// goes through UltraCanvasElementRegistry; a registry miss triggers an
// on-demand scan of the configured plugin directories.
//
// The DSO contract generalizes the proven UltraNet v2 pattern: one exported C
// symbol receives a host vtable, so plugins never resolve host symbols
// (which is what makes Windows work without -rdynamic), and refuse loading
// cleanly on an ABI mismatch.
//
// Version: 1.0.0
// Last Modified: 2026-08-01
// Author: UltraCanvas Framework
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

class UltraCanvasUIElement;   // full definition never needed by the registry

// =============================================================================
// DESCRIPTORS
// =============================================================================

enum class UCPluginCategory { Chart, Diagram, Widget, Tool, FileFormat };

struct UCElementDescriptor {
    std::string typeName;          // stable kebab-case: "kanban-board", "parallel-coordinate-chart"
    UCPluginCategory category = UCPluginCategory::Widget;
    std::string displayName;       // "Kanban Board"
    std::string description;       // one line for pickers/docs
    std::string version;           // plugin-defined
    std::string docPath;           // "Docs/UltraCanvas/UltraCanvasKanbanBoard.md"

    // Mandatory: plain factory, the shape every element already has.
    std::function<std::shared_ptr<UltraCanvasUIElement>(
            const std::string& id, int x, int y, int w, int h)> create;

    // Optional capabilities - set what applies:
    std::function<std::shared_ptr<UltraCanvasUIElement>(
            const std::string& id, int x, int y, int w, int h,
            const std::string& textDefinition,
            std::string* error)> createFromText;   // Mermaid "kanban", "gantt", ...
    std::vector<std::string> textKeywords;         // first word of text definitions
    std::vector<std::string> fileExtensions;       // opens these via FileLoader
    std::vector<std::string> propertyKeys;         // IConfigurableElement keys accepted
};

// =============================================================================
// REGISTRY
// =============================================================================

class UltraCanvasElementRegistry {
public:
    // Idempotent by typeName: re-registering a live name is ignored with a
    // warning (matches the duplicate policy of the existing registries).
    static void Register(const UCElementDescriptor& descriptor);
    static void Unregister(const std::string& typeName);

    // Create by name. A miss first consults the missing-type resolver (or, by
    // default, refreshes the plugin directories) and retries once, which is
    // what makes plugin loading on-demand rather than an eager startup scan.
    static std::shared_ptr<UltraCanvasUIElement> Create(
            const std::string& typeName, const std::string& id,
            int x, int y, int w, int h);

    // Keyword-dispatched text creation: the first word of the definition
    // selects the descriptor ("kanban ..." -> the kanban board). Returns null
    // and fills error (if given) when no keyword matches or creation fails.
    static std::shared_ptr<UltraCanvasUIElement> CreateFromText(
            const std::string& textDefinition, const std::string& id,
            int x, int y, int w, int h, std::string* error = nullptr);

    static const UCElementDescriptor* Find(const std::string& typeName);
    static bool IsRegistered(const std::string& typeName);
    static std::vector<UCElementDescriptor> List(UCPluginCategory category);
    static std::vector<UCElementDescriptor> ListAll();     // bundled + plugged

    // Hook for a smarter lazy loader (e.g. manifest-indexed): called on a
    // Create/CreateFromText miss with the wanted type name; return true if a
    // retry might now succeed. When unset, a miss falls back to
    // UltraCanvas_RefreshElementPlugins().
    static void SetMissingTypeResolver(std::function<bool(const std::string&)> resolver);

    static void Clear();   // tests only: drops every registration and resolver
};

// =============================================================================
// HOST ABI (the DSO contract)
// =============================================================================

// Version of the C entry contract. Grows append-only: a newer host passes a
// larger vtable, an older plugin simply ignores the tail.
inline constexpr int ULTRACANVAS_PLUGIN_HOST_ABI_VERSION = 1;

// Toolchain/stdlib tag guarding the C++ boundary - the thing that actually
// breaks when a plugin is built with a different compiler or standard library.
// Composed at compile time; host and plugin each bake their own and the plugin
// refuses on mismatch instead of crashing later.
#if defined(_MSC_VER)
  #define ULTRACANVAS_ABI_COMPILER "msvc"
  #define ULTRACANVAS_ABI_STDLIB "msvcstl"
#elif defined(__clang__)
  #define ULTRACANVAS_ABI_COMPILER "clang"
#else
  #define ULTRACANVAS_ABI_COMPILER "gcc"
#endif
#ifndef ULTRACANVAS_ABI_STDLIB
  #if defined(_LIBCPP_VERSION)
    #define ULTRACANVAS_ABI_STDLIB "libc++"
  #elif defined(_GLIBCXX_USE_CXX11_ABI) && _GLIBCXX_USE_CXX11_ABI == 0
    #define ULTRACANVAS_ABI_STDLIB "libstdc++-oldabi"
  #else
    #define ULTRACANVAS_ABI_STDLIB "libstdc++"
  #endif
#endif
// "uc1" is the version of the C++ types crossing the boundary
// (UCElementDescriptor & friends); bump it when their layout changes.
#define ULTRACANVAS_FRAMEWORK_ABI_TAG \
    ULTRACANVAS_ABI_COMPILER "-" ULTRACANVAS_ABI_STDLIB "-cxx20-uc1"

#if defined(_WIN32) || defined(_WIN64)
  #define ULTRACANVAS_PLUGIN_EXPORT __declspec(dllexport)
#else
  #define ULTRACANVAS_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

// Function-pointer table handed to the plugin, so the plugin never resolves
// host symbols. Append new members at the end only.
struct UltraCanvasPluginHost {
    int abiVersion;                 // ULTRACANVAS_PLUGIN_HOST_ABI_VERSION
    const char* frameworkVersion;   // e.g. "0.3.21"
    const char* frameworkAbiTag;    // host's ULTRACANVAS_FRAMEWORK_ABI_TAG
    void (*RegisterElement)(const UCElementDescriptor* descriptor);
    void (*Log)(int level, const char* message);   // 0 info, 1 warning, 2 error
};

// The one symbol a plugin exports. Return false to refuse loading (mismatch,
// missing backend, ...): the host skips the plugin with a log line and nothing
// is half-registered.
using UltraCanvas_PluginInitFn = bool (*)(const UltraCanvasPluginHost* host);

// Defines UltraCanvas_PluginInit with the ABI handshake and exception fence
// already in place; `initFn` is bool(const UltraCanvasPluginHost*) and runs
// only after both checks pass.
#define ULTRACANVAS_DEFINE_ELEMENT_PLUGIN(initFn)                              \
    extern "C" ULTRACANVAS_PLUGIN_EXPORT                                       \
    bool UltraCanvas_PluginInit(const UltraCanvas::UltraCanvasPluginHost* host) { \
        if (!host || host->abiVersion < 1 || !host->RegisterElement) return false; \
        if (!host->frameworkAbiTag ||                                          \
            std::string(host->frameworkAbiTag) != ULTRACANVAS_FRAMEWORK_ABI_TAG) { \
            if (host->Log) host->Log(2, "plugin refused: ABI tag mismatch, "   \
                                        "plugin built as " ULTRACANVAS_FRAMEWORK_ABI_TAG); \
            return false;                                                      \
        }                                                                      \
        try { return initFn(host); } catch (...) {                             \
            if (host->Log) host->Log(2, "plugin refused: init threw");         \
            return false;                                                      \
        }                                                                      \
    }

// =============================================================================
// LOADER API
// =============================================================================

// Plugin search directories. Fixed list only - never the working directory,
// never paths taken from documents. Duplicates are ignored; returns false for
// a path that does not name a directory (it is remembered anyway and picked up
// once it exists). The ULTRACANVAS_PLUGIN_PATH environment variable
// (':'-separated, ';' on Windows) is appended on first use.
bool UltraCanvas_AddPluginDirectory(const std::string& directory);

// Scans the configured directories and loads every plugin DSO not yet loaded.
// Idempotent - already-loaded canonical paths are skipped. Returns the number
// of plugins newly loaded; failures are logged, recorded for
// UltraCanvas_GetLastPluginError() and skipped, never fatal. There is no
// unloading: handles stay open for the process lifetime (see proposal §6.4).
int UltraCanvas_RefreshElementPlugins();

// Human-readable reason for the most recent load failure or Create miss.
std::string UltraCanvas_GetLastPluginError();

// Registers the chart types selected at configure time (ULTRACANVAS_CHARTS).
// The body is generated by CMake - possibly empty - so applications can always
// call it unconditionally at startup. Charts not selected here arrive as
// runtime modules through the loader above.
void UltraCanvas_RegisterSelectedCharts();

} // namespace UltraCanvas
