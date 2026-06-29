# Vendored MicroTeX (LaTeX math engine)

This is a trimmed, vendored copy of [MicroTeX](https://github.com/NanoMichael/MicroTeX)
(the `openmath` / Uni-math branch) used by the UltraCanvas LaTeX math plugin.

## What is here

- `lib/` — the MicroTeX engine source (parser, atom/box model, OTF/CLM reader,
  graphic + platform-factory interfaces). This is the upstream source of truth.
- `lib/microtexconfig.h` — provided directly (the upstream CMake normally
  generates this via `configure_file`). `HAVE_AUTO_FONT_FIND` is intentionally
  left undefined.
- `prebuilt/otf2clm.{py,sh}` — the build-time tool that converts an OpenType math
  font into the `.clm2` metrics+glyph-path file. **Not compiled or shipped**;
  kept for reference / regenerating fonts. Requires FontForge with Python.
- `LICENSE-MicroTeX.txt` — MIT license (Copyright 2020 Nano Michael).

## How it is built

LaTeX support is an **on-demand module**: `microtex_core` (this engine) is a
static library linked privately into the shared module `libUltraCanvasLaTeX`,
which the UltraCanvas core `dlopen`s the first time a LaTeX view is created. The
core itself contains only a small loader, never this engine.

The UltraCanvas top-level CMake (`UltraCanvas/CMakeLists.txt`, guarded by
`ULTRACANVAS_PLUGIN_LATEX`) compiles `lib/**/*.cpp` into the static `microtex_core`
target with:

- `-DGLYPH_RENDER_TYPE=1` — **path** glyph rendering. Glyph outlines are emitted
  through the `Graphics2D` path commands, so no platform glyph rasterization is
  needed and math stays crisp at any zoom.
- C++17, exceptions enabled, position-independent code.

The following are **excluded** from the build:

- `lib/wrapper/` — the C-ABI wrapper (for Flutter/JNI/etc.), not used.
- `lib/otf/fontsense.cpp` — the auto-font-find feature and the only
  `std::filesystem` consumer. UltraCanvas initialises the engine with an
  explicit `FontSrcFile` instead.

## Local modifications

Only minimal, clearly-marked changes (search for `[UltraCanvas local patch]`):

- `lib/utils/utils.cpp` — `defaultLocale()` falls back gracefully when
  `en_US.UTF-8` is unavailable on the host instead of throwing.

## Font

The runtime math font (Latin Modern Math OTF + its `.clm2` metrics) lives under
the repository's `media/microtex/` directory, not here. See
`THIRD_PARTY_LICENSES.md` at the repo root.
