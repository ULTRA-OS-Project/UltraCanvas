#pragma once

// Generated configuration for the vendored MicroTeX engine used by UltraCanvas.
//
// This replaces the upstream CMake `configure_file(microtexconfig.h.in ...)`
// step. UltraCanvas builds MicroTeX in "Uni-math" mode with glyphs rendered as
// vector paths (GLYPH_RENDER_TYPE=1), initialised from an explicit FontSrcFile,
// so the auto-font-find feature (the only consumer of std::filesystem) is left
// disabled and `otf/fontsense.cpp` is excluded from the build.
//
// HAVE_AUTO_FONT_FIND intentionally NOT defined.

#define MICROTEX_VERSION_MAJOR 1
#define MICROTEX_VERSION_MINOR 0
#define MICROTEX_VERSION_PATCH 0
