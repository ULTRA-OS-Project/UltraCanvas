// Plugins/UltraCanvasAllFormats.h
// Automatic registration of every format plugin a build produced.
//
// Link the UltraCanvasAllFormats object library and every plugin this build
// contains is registered before main() runs - the FileLoader inventory, the
// Filer's thumbnails and the media viewer's preview pane all see the formats
// with no application code at all. Adding a plugin to the framework then
// reaches every application that links this, instead of every application
// having to learn its name.
//
// The declarations below are for the cases that want to look, not to do the
// registering: a build report, a test that pins what this build carries, or
// an application that (harmlessly) calls the registration itself.
//
// Version: 1.0.0
// Last Modified: 2026-09-17
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_ALL_FORMATS_H
#define ULTRACANVAS_ALL_FORMATS_H

#include <string>
#include <vector>

namespace UltraCanvas {

// Registers every format plugin this build produced, in the order that makes
// the dedicated viewer plugins own the extensions they share with the Vector
// plugin's converters. Called automatically; calling it again is harmless.
void RegisterAllFormatPlugins();

// The plugins that registration covers, by short name ("Vector", "Models",
// "CDR", "XAR", "EPS") - what this build was compiled with, not what the
// framework has. Empty when no plugin was built.
std::vector<std::string> AutoRegisteredFormatPlugins();

} // namespace UltraCanvas

#endif // ULTRACANVAS_ALL_FORMATS_H
