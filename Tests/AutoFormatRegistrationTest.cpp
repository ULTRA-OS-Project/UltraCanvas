// Tests/AutoFormatRegistrationTest.cpp
// Linking UltraCanvasAllFormats must be the whole of it: every format plugin
// the build produced is registered before main() runs, with no application
// code anywhere.
//
// This file calls no Register*Plugin(). If the assertions below hold, they
// hold because the object library's registrar ran on its own - which is the
// property worth pinning, because two things can quietly break it. A static
// library would let the linker drop the registrar (nothing references it),
// and that is why the registrar lives in an OBJECT library. And a registry
// whose storage was a namespace-scope static could be registered into before
// it was constructed, which is why the graphics registry's storage is
// function-local now.
//
// Version: 1.0.0
// Last Modified: 2026-09-17
// Author: UltraCanvas Framework

#include "UltraCanvasAllFormats.h"
#include "UltraCanvasGraphicsPluginSystem.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasSupportedFormats.h"
#include "UltraCanvasVectorPreview.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what.c_str());
    } else {
        std::printf("  ok: %s\n", what.c_str());
    }
}

bool Built(const std::string& plugin) {
    const std::vector<std::string> built = AutoRegisteredFormatPlugins();
    return std::find(built.begin(), built.end(), plugin) != built.end();
}

bool RegistryLoads(const std::string& extension) {
    const std::vector<std::string> extensions =
            UltraCanvasGraphicsPluginRegistry::GetSupportedExtensions();
    return std::find(extensions.begin(), extensions.end(), extension) !=
           extensions.end();
}

}   // namespace

int main(int argc, char** argv) {
    UCImage::InitializeImageSubsysterm(argv[0]);

    const std::vector<std::string> built = AutoRegisteredFormatPlugins();
    std::printf("== plugins this build carries:");
    for (const std::string& plugin : built) std::printf(" %s", plugin.c_str());
    std::printf("%s\n", built.empty() ? " (none)" : "");

    // Nothing above registered anything. Whatever is registered got there by
    // itself.
    std::printf("== registered without a single call\n");
    const auto registered = UltraCanvasGraphicsPluginRegistry::GetAllPlugins();
    Check(registered.size() >= built.size(),
          "every built plugin reached the registry (" +
                  std::to_string(registered.size()) + " registered, " +
                  std::to_string(built.size()) + " built)");

    if (Built("Vector")) {
        Check(RegistryLoads("dwg") && RegistryLoads("dxf"),
              "Vector: the registry loads dwg and dxf");
        // Registration also installs the core-side preview seam, which is
        // what the media viewer and the Filer's thumbnails read.
        Check(CanPreviewVectorExtension("dwg"),
              "Vector: the preview seam answers for dwg");
        auto dwg = UltraCanvasSupportedFormats::FindByExtension("dwg");
        Check(dwg && dwg->canLoad, "Vector: the inventory reports dwg loadable");
    }
    if (Built("Models")) {
        Check(RegistryLoads("obj") && RegistryLoads("fbx"),
              "Models: the registry loads obj and fbx");
    }
    if (Built("CDR")) {
        Check(RegistryLoads("cdr") && RegistryLoads("cmx"),
              "CDR: the registry loads cdr and cmx");
    }
    if (Built("XAR")) {
        Check(RegistryLoads("xar"), "XAR: the registry loads xar");
    }
    if (Built("EPS")) {
        Check(RegistryLoads("eps"), "EPS: the registry loads eps");
    }

    // Ownership of a shared extension goes to the dedicated viewer, because
    // the registrar puts it after the Vector plugin and the last registration
    // wins. Reversing that order would silently downgrade these formats.
    if (Built("Vector") && Built("XAR")) {
        auto owner = UltraCanvasGraphicsPluginRegistry::GetPluginByName(
                "UltraCanvas Vector Formats Plugin");
        Check(owner != nullptr, "the Vector plugin is registered alongside XAR");
        auto element = UltraCanvasGraphicsPluginRegistry::GetFileInfo("drawing.xar");
        Check(element.formatType == GraphicsFormatType::Vector,
              "xar is still a vector format with both registered");
    }

    // Calling it by hand after the automatic pass must change nothing.
    const size_t before = UltraCanvasGraphicsPluginRegistry::GetAllPlugins().size();
    RegisterAllFormatPlugins();
    Check(UltraCanvasGraphicsPluginRegistry::GetAllPlugins().size() == before,
          "registering again is a no-op");

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
