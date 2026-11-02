// OS/Linux/UltraCanvasLinuxFonts.cpp
// The font side of a Linux application. See the header.
// Version: 1.0.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework

#include "UltraCanvasLinuxFonts.h"

#include "UltraCanvasApplication.h"   // kEmbeddedAllFonts, GetBundledFontsDir
#include "UltraCanvasDebug.h"
#include "UltraCanvasEmojiSettings.h"

#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <filesystem>
#include <sstream>

namespace UltraCanvas {
namespace LinuxFonts {

    FontStyle DetectSystemFontStyle() {
        FontStyle result;
        result.fontFamily = "Ubuntu";
        result.fontSize = 12.0;
        return result;
    }

    FontStyle DetectMonospacedFontStyle() {
        FontStyle result;
        result.fontFamily = "Ubuntu Mono";
        result.fontSize = 12.0;
        return result;
    }

    void LoadBundledFonts() {
        FcConfig* cfg = FcConfigGetCurrent();
        if (!cfg) {
            debugOutput << "UltraCanvas: FcConfigGetCurrent() returned null; bundled fonts not registered" << std::endl;
            return;
        }

        const std::string dir = GetBundledFontsDir();
        for (size_t i = 0; i < kEmbeddedAllFontsCount; ++i) {
            std::string path = dir + kEmbeddedAllFonts[i];
            if (!std::filesystem::exists(path)) {
                debugOutput << "UltraCanvas: bundled font missing: " << path << std::endl;
                continue;
            }
            if (!FcConfigAppFontAddFile(cfg,
                    reinterpret_cast<const FcChar8*>(path.c_str()))) {
                debugOutput << "UltraCanvas: FcConfigAppFontAddFile failed for " << path << std::endl;
            }
        }
        // The emoji font the emoji setting picks (UOS-Settings), and the rule
        // that draws emoji in it.
        ApplyEmojiSettings();
        // Materialise the FontSet so FcMatch (and therefore Pango) can
        // actually find the just-added app fonts. No-op on a system-loaded
        // config; required on a FcConfigCreate'd fallback one.
        FcConfigBuildFonts(cfg);

        // Force an FC-backed font map as the default, replacing whatever
        // default was cached. On Linux this is a no-op pattern (Pango is
        // already FC-only); on Windows MSYS2 it bypasses Pango's Win32
        // backend default. set_default takes its own ref so we drop ours.
        PangoFontMap* fcFm = pango_cairo_font_map_new_for_font_type(CAIRO_FONT_TYPE_FT);
        if (fcFm) {
            pango_cairo_font_map_set_default(PANGO_CAIRO_FONT_MAP(fcFm));
            g_object_unref(fcFm);
        } else {
            debugOutput << "UltraCanvas: pango_cairo_font_map_new_for_font_type(FT) returned null" << std::endl;
            pango_cairo_font_map_set_default(nullptr);
        }

#ifdef ULTRACANVAS_DEBUG
        // Diagnostic: how many fonts does FC know about now? If this is small
        // (<10) on a packaged build, FC likely couldn't find any system fonts
        // and only our bundled DejaVu families are available. Also log a
        // sample of family names so we can tell whether DejaVu Sans was
        // actually picked up by FC's dir scan.
        {
            FcPattern* pat = FcPatternCreate();
            FcObjectSet* os = FcObjectSetBuild(FC_FAMILY, FC_FILE, (char*)nullptr);
            FcFontSet* fs = FcFontList(cfg, pat, os);
            debugOutput << "UltraCanvas: FC font count after init=" << (fs ? fs->nfont : -1) << std::endl;
            if (fs) {
                int sampleN = std::min(fs->nfont, 500);
                std::ostringstream fams;
                for (int i = 0; i < sampleN; ++i) {
                    FcChar8* fam = nullptr;
                    if (FcPatternGetString(fs->fonts[i], FC_FAMILY, 0, &fam) == FcResultMatch && fam) {
                        if (i) fams << ", ";
                        fams << reinterpret_cast<const char*>(fam);
                    }
                }
                debugOutput << "UltraCanvas: FC family sample (" << sampleN << "/" << fs->nfont
                            << "): " << fams.str() << std::endl;
                FcFontSetDestroy(fs);
            }
            FcObjectSetDestroy(os);
            FcPatternDestroy(pat);
        }
#endif
    }

    bool RegisterFontFile(const std::string& fontFilePath) {
        // An application font: added to this process's FcConfig only, so
        // nothing is installed for the user or for other applications.
        FcConfig* cfg = FcConfigGetCurrent();
        if (!cfg) {
            debugOutput << "UltraCanvas: RegisterFontFileNative: no current "
                           "fontconfig config" << std::endl;
            return false;
        }
        if (!FcConfigAppFontAddFile(
                cfg, reinterpret_cast<const FcChar8*>(fontFilePath.c_str()))) {
            debugOutput << "UltraCanvas: FcConfigAppFontAddFile failed for "
                        << fontFilePath << std::endl;
            return false;
        }
        return true;
    }

} // namespace LinuxFonts
} // namespace UltraCanvas
