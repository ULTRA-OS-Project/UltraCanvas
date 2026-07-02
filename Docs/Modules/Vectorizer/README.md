# Vectorizer — Raster → SVG plugin

**Status:** Phase 1 implemented · module path
`UltraCanvas/Plugins/Vectorizer/`

Sibling plugin to `OCR` and `PDF`. Converts raster images (PNG / JPEG /
WebP / TIFF / anything libvips can decode, plus arbitrary
`PFXImage` buffers) into colour or B&W SVG vector graphics.

The demo's `vectorizer` tool entry
(`Apps/DemoApp/UltraCanvasDemo.cpp:1245`) is the immediate consumer;
follow-ups will plug it into the Image element ("Trace bitmap…"
context action) and the PDF viewer ("Vectorize page" for scanned
diagrams).

---

## 1. Goals

1. **Quality colour tracing** — output usable as design-grade SVG
   (logos, illustrations, icons), not just B&W silhouettes.
2. **Cross-platform** — Linux / macOS / Windows, same build pattern
   as the other plugins (`pkg_check_modules` + soft-fail when a
   prerequisite is missing).
3. **Compatible license** — MIT-compatible only. Rules out Potrace
   and Autotrace (GPL).
4. **Plays with the framework's image type** — `PFXImage`
   (libvips-backed) in, SVG string out.

Non-goals: SVG editing (the existing SVG plugin handles parsing /
rendering of already-vector data), CAD-grade B-spline fitting,
multi-page vector documents.

---

## 2. Library survey

| Library | Lang | License | Colour | Quality | Build |
|---|---|---|---|---|---|
| **VTracer** | Rust | MIT | Yes (clustered) | Excellent | Cargo |
| **Potrace** | C | GPLv2 / paid | No (B&W) | Excellent for B&W | Trivial |
| **Autotrace** | C | GPL | Limited | OK / dated | Autotools |
| **ImageTracer-cpp** | C++ | MIT | Yes (clustered) | Lower than VTracer | Header-only |
| **OpenCV `findContours`** | C++ | Apache-2.0 | No (just contours) | Building block | Heavy dep |

### Recommendation

**VTracer** is the only library that gives MIT-compatible
*colour* tracing of the quality real users expect from a "Trace
Bitmap" feature in 2026. The fact that it's written in Rust is a
build-system question, not a correctness one — and Rust integrates
cleanly into CMake via [`corrosion-rs/corrosion`](https://github.com/corrosion-rs/corrosion).

A B&W-only fallback through ImageTracer-cpp could ship as Tier 2
behind `ULTRACANVAS_VECTORIZER_IMAGETRACER=ON` if the Rust toolchain
must not appear in a build (e.g. minimal embedded variants). Not in
the initial cut.

---

## 3. Architecture

Three layers — same shape as the OCR plugin, but with an extra Rust
shim because VTracer is Rust:

```
UltraCanvas/Plugins/Vectorizer/
├── CMakeLists.txt                         # corrosion + C++ static lib
├── include/
│   └── UltraCanvasVectorizerPlugin.h      # public C++ API
├── src/
│   └── UltraCanvasVectorizerPlugin.cpp    # facade + PFXImage→RGBA bridge
└── vtracer-c/                             # Rust shim crate
    ├── Cargo.toml                         # depends on vtracer = "0.7"
    ├── cbindgen.toml                      # (optional) header generation
    ├── include/
    │   └── ultracanvas_vtracer.h          # hand-written C header
    └── src/
        └── lib.rs                         # #[no_mangle] extern "C" entry points
```

**Build flow (CMake):**

1. `ULTRACANVAS_PLUGIN_VECTORIZER=ON` is requested.
2. CMake checks `find_program(CARGO cargo)`. Missing → print
   `[✗] Vectorizer Plugin - DISABLED (cargo not found)`, skip.
3. Pull `corrosion` via `FetchContent` (pinned tag), call
   `corrosion_import_crate(MANIFEST_PATH vtracer-c/Cargo.toml)`.
   Corrosion compiles the Rust crate as a static library and exposes
   it as the CMake target `vtracer_c`.
4. The C++ static library `UltraCanvasVectorizerPlugin` links
   `vtracer_c` plus the standard `vips-cpp` / `glib` / UltraCanvas
   includes.

Cargo target is `staticlib` so the final binary has no Rust runtime
dependencies beyond the C ABI surface.

---

## 4. Public API (C++)

```cpp
namespace UltraCanvas {

enum class VectorizerColorMode { Binary, Color };
enum class VectorizerHierarchy { Stacked, Cutout };
enum class VectorizerPathMode  { Pixel, Polygon, Spline };

struct VectorizerConfig {
    VectorizerColorMode  colorMode       = VectorizerColorMode::Color;
    VectorizerHierarchy  hierarchy       = VectorizerHierarchy::Stacked;
    VectorizerPathMode   pathMode        = VectorizerPathMode::Spline;
    int     filterSpeckle   = 4;     // drop blobs smaller than N pixels
    int     colorPrecision  = 6;     // significant bits per colour channel
    int     layerDifference = 16;    // cluster merge threshold (0..255)
    int     cornerThreshold = 60;    // degrees; smaller = more corners
    double  lengthThreshold = 4.0;   // min path length in pixels
    int     spliceThreshold = 45;    // degrees; controls curve splicing
    int     maxIterations   = 10;    // path-fitting iterations
};

struct VectorizerResult {
    std::string svg;          // complete <svg>…</svg> document
    int         width  = 0;
    int         height = 0;
    double      elapsedMs = 0.0;
    std::string error;        // empty on success
    bool Ok() const { return error.empty(); }
};

class UltraCanvasVectorizer {
public:
    explicit UltraCanvasVectorizer(const VectorizerConfig& cfg = {});
    ~UltraCanvasVectorizer();

    VectorizerResult VectorizeFile  (const std::string& path);
    VectorizerResult VectorizeBuffer(const void* data, size_t bytes,
                                     const std::string& formatHint = "");
    VectorizerResult VectorizeImage (const PixelFX::PFXImage& img);

    std::future<VectorizerResult> VectorizeImageAsync(
        const PixelFX::PFXImage& img, std::stop_token = {});

    void                  SetConfig(const VectorizerConfig& cfg);
    const VectorizerConfig& Config() const;

    static bool        IsAvailable();           // true when FFI linked OK
    static const char* BackendVersion();        // VTracer version string
};

} // namespace UltraCanvas
```

`PFXImage` → Rust bridge: convert the libvips image to 8-bit RGBA,
hand the raw buffer to `ultracanvas_vtracer_from_rgba(...)`. Rust
returns SVG as a heap-allocated UTF-8 string; the C++ side copies it
into `std::string` and calls `ultracanvas_vtracer_free()`. No
ownership ambiguity, no leaks possible from the C++ side.

---

## 5. Rust shim (`vtracer-c`)

Tiny crate. Exports exactly four C functions:

```c
typedef struct {
    uint8_t color_mode;        // 0=Binary, 1=Color
    uint8_t hierarchy;         // 0=Stacked, 1=Cutout
    uint8_t path_mode;         // 0=Pixel, 1=Polygon, 2=Spline
    int32_t filter_speckle;
    int32_t color_precision;
    int32_t layer_difference;
    int32_t corner_threshold;
    double  length_threshold;
    int32_t splice_threshold;
    int32_t max_iterations;
} UCVectorizerConfig;

typedef struct {
    char*   svg_utf8;          // heap-allocated, NUL-terminated; NULL on error
    size_t  svg_len;
    char*   error_utf8;        // heap-allocated, NUL-terminated; NULL on success
} UCVectorizerOutput;

// Returns 0 on success, non-zero on error (error_utf8 set in the latter case).
int32_t ultracanvas_vtracer_from_rgba(
    const uint8_t*               rgba,
    int32_t                      width,
    int32_t                      height,
    const UCVectorizerConfig*    config,
    UCVectorizerOutput*          out);

void        ultracanvas_vtracer_free   (UCVectorizerOutput* out);
const char* ultracanvas_vtracer_version(void);
const char* ultracanvas_vtracer_last_error(void); // thread-local last-error
```

Memory rule: every pointer the Rust side returns is owned by Rust
and must be freed via `ultracanvas_vtracer_free`. The `rgba` input
buffer is borrowed for the duration of the call only.

Panics are caught with `catch_unwind` and converted to error
strings — Rust panics never cross the FFI boundary.

---

## 6. Build dependency on `cargo`

Adds **one** build-time prerequisite: a working Rust toolchain on
PATH (`cargo` + `rustc`). Same shape as Tesseract or Poppler — the
plugin is opt-in (`ULTRACANVAS_PLUGIN_VECTORIZER=ON`, off by default)
and configure-time-detected:

```cmake
if(ULTRACANVAS_PLUGIN_VECTORIZER)
    find_program(CARGO cargo)
    if(CARGO)
        add_subdirectory(Plugins/Vectorizer)
        message(STATUS "  [✓] Vectorizer Plugin - ENABLED")
    else()
        message(STATUS "  [✗] Vectorizer Plugin - DISABLED (cargo not found)")
    endif()
endif()
```

Install instructions (added to the top-level README in a follow-up):

| OS | Command |
|---|---|
| Linux | `sudo apt install cargo rustc` *or* the rustup one-liner |
| macOS | `brew install rustup-init && rustup-init -y` |
| Windows | <https://rustup.rs/> (`rustup-init.exe`) |

Cross-compilation works the standard way:
`rustup target add x86_64-pc-windows-msvc` then build with the
existing CMake toolchain — corrosion picks the target up from the
CMake variables.

Binary cost: ~3–5 MB added to the final library; no runtime
dependencies.

---

## 7. UI integration (phase 2)

* **`UltraCanvasImageElement::TraceToSVG()`** — produces a
  `VectorizerResult`, optionally swaps the bitmap presentation for
  the parsed SVG via the existing SVG plugin so the user can keep
  zooming without resolution loss.
* **PDF viewer "Vectorize page" action** — for scanned diagrams /
  schematics, produce an SVG sidecar.
* **File-format export hook** — `PixelFX` exporters gain `*.svg` as
  an output format when the plugin is linked.
* **Demo screen** — drop image, configure colour mode / speckle /
  precision sliders, preview both the source bitmap and the vector
  output side by side.

---

## 8. Risks

* **Rust toolchain in build environments.** CI images that don't
  ship `cargo` won't build the plugin — same caveat as
  Tesseract / Poppler not being installed. Default-off, soft-fail
  configure, install instructions documented.
* **`corrosion` version drift.** Pin to a known tag in
  `FetchContent_Declare`. Re-pin on intentional upgrades.
* **VTracer upstream churn.** Pin the `vtracer` crate version in
  `Cargo.toml` (`vtracer = "=0.7.x"`) so a `cargo update` can't
  surprise the build.
* **MSVC C-runtime mismatch.** Rust on Windows defaults to the same
  MSVC CRT as CMake's default; if a user changes runtime flavour,
  corrosion exposes the toggle. Documented in §6.
* **Output size on photographs.** Tracing a 4-megapixel photo
  produces gigantic SVGs. The facade exposes
  `filterSpeckle` / `colorPrecision` defaults tuned for
  logos/icons; for photographs, callers should down-sample
  through `PFXImage` first (libvips makes that one call).

---

## 9. Phased delivery

| Phase | Scope | Effort |
|---|---|---|
| **P1** | Plugin skeleton, corrosion wire-up, Rust shim, C++ facade, file/buffer/PFXImage entry points, demo screen pointing at this README | shipped now |
| **P2** | UI hooks: image-element trace action, PDF viewer "Vectorize page", PixelFX `.svg` export | ~2 days |
| **P3** | Cancellation + parallel batch (vectorise N images / pages) | ~1 day |
| **P4** | Optional ImageTracer-cpp Tier-2 backend behind `ULTRACANVAS_VECTORIZER_IMAGETRACER=ON` for zero-Rust builds | ~1 day |

---

## 10. Recommendation

P1 lands the Tesseract-equivalent baseline: an opt-in plugin
behind `ULTRACANVAS_PLUGIN_VECTORIZER`, a documented FFI surface, a
working C++ facade, and the demo screen wired through. Quality is
already production-grade because all the heavy lifting is VTracer.
UI integration (P2) is the next step once UX decisions are made.
