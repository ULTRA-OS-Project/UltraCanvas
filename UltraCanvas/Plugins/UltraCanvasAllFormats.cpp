// Plugins/UltraCanvasAllFormats.cpp
// Every format plugin this build produced, registered before main() runs.
//
// A plugin reads nothing until somebody registers it, and until now that
// "somebody" was each application. UltraFiler registered none, so the whole
// Vector matrix and every 3D format but STL sat compiled into the binary and
// unusable - the readers were there, nothing had introduced them. Each new
// application would have had to learn the same list, and each new plugin
// would have had to be added to every application that wanted it.
//
// So the list lives here instead, once, and the build fills it in: CMake
// defines ULTRACANVAS_HAS_<PLUGIN> for each plugin target that was actually
// built, and the registrar below calls what those defines admit exist. An
// application gets every format its build can read by linking this and
// writing nothing.
//
// It is an OBJECT library on purpose. A static library hands the linker
// object files and the linker keeps only the ones something references -
// nothing references a registrar, so in a .a this file would be dropped and
// the registration would silently never happen. Object libraries have no
// such rule: every object goes in.
//
// Registration order is the ownership order. The graphics registry lets the
// last registration win a shared extension, and the dedicated viewer plugins
// (CorelDRAW through libcdr, Xara's own record grammar, the EPS interpreter)
// read some of the same extensions as the Vector plugin's converters and
// read them better - so they come after it. The Vector plugin keeps what
// only it reads (DXF, the DWG family, EMF, WMF) and stays the only writer,
// since save dispatch matches on GetSaveExtensions instead.
//
// Version: 1.0.0
// Last Modified: 2026-09-17
// Author: UltraCanvas Framework

#ifdef ULTRACANVAS_HAS_VECTOR_PLUGIN
#include "Vector/UltraCanvasVectorFormatsPlugin.h"
#endif
#ifdef ULTRACANVAS_HAS_MODELS_PLUGIN
#include "Models/UltraCanvasModelFormatsPlugin.h"
#endif
#ifdef ULTRACANVAS_HAS_CDR_PLUGIN
#include "Vector/CDR/UltraCanvasCDRPlugin.h"
#endif
#ifdef ULTRACANVAS_HAS_XAR_PLUGIN
#include "Vector/XAR/UltraCanvasXARPlugin.h"
#endif
#ifdef ULTRACANVAS_HAS_EPS_PLUGIN
#include "Vector/EPS/UltraCanvasEPSPlugin.h"
#endif

#include "UltraCanvasAllFormats.h"

namespace UltraCanvas {

void RegisterAllFormatPlugins() {
    // Idempotent: every Register*Plugin() refuses a second registration of a
    // plugin of the same name, so calling this by hand after the automatic
    // one has run costs a few string comparisons and changes nothing.
#ifdef ULTRACANVAS_HAS_VECTOR_PLUGIN
    RegisterVectorFormatsPlugin();
#endif
#ifdef ULTRACANVAS_HAS_MODELS_PLUGIN
    RegisterModelFormatsPlugin();
#endif
    // After the Vector plugin - see the note at the top of this file.
#ifdef ULTRACANVAS_HAS_CDR_PLUGIN
    RegisterCDRPlugin();
#endif
#ifdef ULTRACANVAS_HAS_XAR_PLUGIN
    RegisterXARPlugin();
#endif
#ifdef ULTRACANVAS_HAS_EPS_PLUGIN
    RegisterEPSPlugin();
#endif
}

std::vector<std::string> AutoRegisteredFormatPlugins() {
    return {
#ifdef ULTRACANVAS_HAS_VECTOR_PLUGIN
        "Vector",
#endif
#ifdef ULTRACANVAS_HAS_MODELS_PLUGIN
        "Models",
#endif
#ifdef ULTRACANVAS_HAS_CDR_PLUGIN
        "CDR",
#endif
#ifdef ULTRACANVAS_HAS_XAR_PLUGIN
        "XAR",
#endif
#ifdef ULTRACANVAS_HAS_EPS_PLUGIN
        "EPS",
#endif
    };
}

namespace {

// The registrar. Runs before main(), which is what makes this automatic.
// Safe against static-initialisation order because everything it touches is
// a function-local static: the graphics registry's storage, the model and
// vector preview provider slots, and the mutexes guarding them are all built
// on first use rather than during static initialisation.
const bool formatPluginsRegistered = [] {
    RegisterAllFormatPlugins();
    return true;
}();

}   // namespace

}   // namespace UltraCanvas
