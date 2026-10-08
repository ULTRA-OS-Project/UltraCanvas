- Tests: `PublicHeadersUnusedParamTest` includes the framework's 315 public
  headers (313 without GL) in one file compiled with
  `-Werror=unused-parameter`, so an inline body that leaves a parameter
  unused fails the build instead of warning in every `-Wextra` build. It
  leaves out the optional subsystems whose headers need libraries a build
  may lack (UltraNet, UltraVault, PixelFX, LaTeX, the databases, IO devices,
  network monitor, messaging and window-server clients), and the GL surface
  headers when GL is off. GCC and Clang only - every CI leg, Windows
  included through MSYS2's clang; checked here with clang and GCC on Linux
  and with MinGW GCC against the Windows headers. `AGENTS.md` states the
  rule.
- `scripts/check_doc_examples.py` checks a copied type against every type
  of its name and judges it by the best match. A short name can name
  several types - `BlendMode` is the render context's enum, a PixelFX one
  and a VectorStorage one - and the checker took the first in sorted order,
  `PixelFX::BlendMode`, so `UltraCanvasRenderContext.md`'s correct listing
  of the render context's modes read as seven missing enumerators. A
  planted wrong enumerator is still reported, against
  `UltraCanvas::BlendMode`. The page's mask example declares the
  application's `DrawContent` it calls; the page passes the checker.
