# UltraCanvasLaTeXView

Renders a LaTeX (math) formula as a native UltraCanvas UI element, using an
embedded [MicroTeX](https://github.com/NanoMichael/MicroTeX) engine. The formula
is typeset to **vector paths** and drawn through the element's `IRenderContext`,
so it participates in the normal layout / transform / clip pipeline and stays
crisp at any zoom — there is no rasterized surface to blit.

## Load-on-demand module

LaTeX support is an **on-demand module**. The MicroTeX engine and its math font
live in a separate shared object — `libUltraCanvasLaTeX.so` (`.dylib` / `.dll`) —
that the core loads via `dlopen` the **first time** `CreateLaTeXView()` is called.
Until then nothing LaTeX-related (engine code or font) is mapped into the process.

Consequently `UltraCanvasLaTeXView` is an **abstract interface** (a normal
`UltraCanvasUIElement`); concrete instances come from the factory. Application
code never includes the MicroTeX headers or the module's include paths — only:

```cpp
#include "Plugins/LaTeX/UltraCanvasLaTeXView.h"
```

## Basic usage

```cpp
using namespace UltraCanvas;

auto formula = CreateLaTeXView("eq1", /*x*/20, /*y*/20, /*w*/0, /*h*/0);
if (formula) {                          // null only if the module can't be loaded
    formula->SetTextSize(28.0f);
    formula->SetTextColor(Colors::Black);
    formula->SetLaTeX("\\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}");
    window->AddChild(formula);
}
```

`CreateLaTeXView` returns a `std::shared_ptr<UltraCanvasLaTeXView>`. The first
call triggers the `dlopen`; later calls reuse the already-loaded module. Width/
height of `0` lets the element size itself to the formula via the layout engine.

## API

### Free functions (in the core)
| Function | Description |
| --- | --- |
| `CreateLaTeXView(id,x,y,w,h)` | Load the module on demand and create a view. Returns `nullptr` if the module is unavailable. |
| `IsLaTeXModuleAvailable()` | Force a load attempt (idempotent) and report whether LaTeX is usable. |
| `GetLaTeXModuleError()` | Reason the last load attempt failed (empty on success). |
| `SetLaTeXModulePath(pathOrDir)` | Override where the module is looked up. Call before first use. |
| `SetLaTeXFontSearchDir(dir)` | Override where the bundled math font is looked up. |
| `UnloadLaTeXModule()` | Unload the module (shutdown only; after all views are destroyed). |

### `UltraCanvasLaTeXView` (the element)
| Method | Description |
| --- | --- |
| `SetLaTeX(const std::string&)` / `GetLaTeX()` | The LaTeX source (math mode). Empty input renders nothing. |
| `SetTextSize(float)` / `GetTextSize()` | Font size in pixels (default 20); drives intrinsic size. |
| `SetTextColor(const Color&)` / `GetTextColor()` | Foreground color (default black). |
| `SetMaxWidth(float)` | Wrap width for inter-line math; `0` (default) = intrinsic width. |
| `IsValid()` / `GetLastError()` | Whether the last `SetLaTeX` produced a render, and why not. |

## Where the module and font are found

On first use the loader searches for the module in this order:
1. a path set via `SetLaTeXModulePath(...)`,
2. `$ULTRACANVAS_PLUGIN_DIR`,
3. `<exe>/`, `<exe>/plugins/`, `<exe>/../lib/`, `<exe>/../lib/ultracanvas/`,
4. the dynamic linker's own search path (rpath / `LD_LIBRARY_PATH`).

The module then loads `latinmodern-math.clm2` (+ `.otf`) from `media/microtex`
relative to the executable (or `$MICROTEX_FONTDIR`, or `SetLaTeXFontSearchDir`).
The top-level CMake copies `media/` into the build/install tree.

If `CreateLaTeXView` returns `nullptr`, check `GetLaTeXModuleError()`.

## Build / deployment

- Built when `ULTRACANVAS_PLUGIN_LATEX=ON` (default): produces the core-side
  loader plus the `libUltraCanvasLaTeX` module. `OFF` builds neither.
- **Recommended:** build the core as a shared library
  (`-DULTRACANVAS_BUILD_SHARED=ON`) — the natural form for an OS module — so the
  plugin resolves core symbols against `libUltraCanvas`. This is the validated
  configuration.
- With a **static** core, the host executable must export its dynamic symbols so
  the `dlopen`'d module can resolve them; the bundled apps already set
  `ENABLE_EXPORTS` for this.

## Examples

```cpp
formula->SetLaTeX("\\int_{0}^{\\infty} e^{-x^2}\\,dx = \\frac{\\sqrt{\\pi}}{2}");
formula->SetLaTeX("\\sum_{n=1}^{\\infty} \\frac{1}{n^2} = \\frac{\\pi^2}{6}");
formula->SetLaTeX("\\begin{pmatrix} a & b \\\\ c & d \\end{pmatrix}");
formula->SetLaTeX("\\text{if } x \\geq 0 \\text{ then } \\sqrt{x}\\in\\mathbb{R}");
```

## Notes

- Renders **math-mode** LaTeX (equations, symbols, matrices, fractions, roots,
  big operators, accents). It is not a full document typesetter.
- Glyphs are drawn as filled vector paths (`GLYPH_RENDER_TYPE=1`); `\text{...}`
  runs use the framework's Pango text layout.
- The C ABI between core and module is in `UltraCanvasLaTeXModuleABI.h`
  (versioned via `ULTRACANVAS_LATEX_ABI_VERSION`).
- See `THIRD_PARTY_LICENSES.md` for MicroTeX (MIT) and Latin Modern Math
  (GUST/OFL) licensing.
