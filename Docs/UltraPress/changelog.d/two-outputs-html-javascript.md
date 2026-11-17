- **A site is published as HTML + JavaScript or as WebAssembly.** The new
  `"output"` in `site.json` chooses which; it is also *Publish as* in *Site
  settings…*, and `ultrapress publish --output html|wasm` for one run.
  `html`, the default for new sites and for sites that do not say, writes
  every page through UltraSite:
  - styled pages, built from the blocks and the theme's colours, with a
    4 KB script for prefetch and the theme toggle;
  - a hashed stylesheet;
  - `feed.xml`, `sitemap.xml`, `404.html`, `.htaccess`, `_headers` and the
    media folder.

  The example site's front page is 2.3 KB compressed, with 3.0 KB of
  stylesheet and 0.8 KB of script that every page shares. A broken link,
  a missing anchor or a page over its size budget fails the publish;
  `--sizes` lists what everything weighs. `wasm` writes the HTML shadow for
  the WebAssembly renderer, as before.
  - **The dark look:** `"dark"` in `theme.json` gives a site's colours for
    it. The site then follows the visitor's system and offers a toggle that
    remembers the choice. The example site has one.
  - **Outputs per block:** a block type states which outputs can show it
    (`BlockType::htmlOutput`, `wasmOutput`), and `check` reports a block the
    site's output cannot show. Every built-in block has both.
  - **Engine:** `UltraPressSite.h` (`PublishHtml`, `Publish`, `ThemeTokens`,
    `BlockStylesheet`); `HtmlEmitter::Parts`; `HtmlOptions::styled`, which
    fetches the hero's picture first and every other picture lazily, and
    makes the FAQ an accordion that keeps one answer open. The shadow is
    unchanged.
