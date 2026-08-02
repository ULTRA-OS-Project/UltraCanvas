// Tests/TestRefusingPlugin.cpp
// A plugin DSO that refuses to load (a missing backend, say). The loader must
// skip it with a recorded error and register nothing.
//
// Version: 1.0.0
// Last Modified: 2026-08-01
// Author: UltraCanvas Framework

#include "UltraCanvasElementPlugins.h"
#include <string>

using namespace UltraCanvas;

namespace {

bool InitRefusingPlugin(const UltraCanvasPluginHost*) {
    return false;   // per the contract: refuse before registering anything
}

} // namespace

ULTRACANVAS_DEFINE_ELEMENT_PLUGIN(InitRefusingPlugin)
