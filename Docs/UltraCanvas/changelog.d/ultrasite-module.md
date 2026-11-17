- **UltraSite: a new module that writes a website as HTML + JavaScript.**
  It works the way Docusaurus builds a site, without React, hydration or a
  bundler. A list of routes becomes a folder any static host serves:
  - every route as a complete, styled page: head, skip link, navbar, the
    `page` or `post` layout or an app's own, and footer;
  - one stylesheet built from the base, the site's design tokens (a light
    and an optional dark look, as `--us-*` custom properties, with a toggle
    that never flashes) and the app's rules;
  - `core.js` on every page (prefetch on intent, the theme toggle), and
    behaviour modules an app registers, each loaded only by the pages whose
    body names it with `data-us`;
  - the stylesheet and the scripts named by their content hash and cached
    for a year;
  - `sitemap.xml`, `robots.txt`, `404.html`, `.htaccess` and `_headers`,
    with a Content-Security-Policy that runs no script from another origin;
  - speculation rules, so Chromium prerenders the next page.

  The build reads every page back through the HTMLReader. It fails on a
  broken internal link or anchor, an unknown behaviour, or a page or asset
  over its compressed-size budget, and it removes what an earlier build
  left behind. Pages work with JavaScript turned off.
  - `UltraSite/` (target `UltraSite`, header `<UltraSite/UltraSite.h>`) and
    `Docs/Modules/UltraSite/README.md`. The design is
    `Docs/Research/UltraSiteStaticSitesProposal.md`.
  - Tests: `Tests/UltraSite` (ctest `UltraSite`, built with
    `-DULTRACANVAS_BUILD_ULTRASITE_TESTS=ON`), run by the build workflow on
    every platform.
  - `scripts/check_doc_examples.py` now compiles the module's README.
