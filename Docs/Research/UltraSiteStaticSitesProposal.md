# UltraSite — Docusaurus-style static HTML sites for UltraPress and UltraStore (Investigation and Proposal)

**Date:** 2026-11-17
**Status:** Proposal — investigation only, no implementation yet
**Scope:** one shared, headless static-site generator, `UltraSite`, that
UltraPress (`Apps/UltraPress`) and the UltraStore admin backend
(`Apps/UltraStoreAdmin`) both publish through. It writes fast, fully styled
HTML websites the way [Docusaurus](https://docusaurus.io) does: every page
is rendered to its own HTML file when the site is built, and the browser
gets pages it can show straight away, with no rendering on its side. The
example the request named is <https://docs.onramp.ltd/>.

This proposal changes earlier decisions. Both apps publish WebAssembly
today: UltraPress's design ([`UltraPressDesignProposal.md`](UltraPressDesignProposal.md)
§1, §4.4) and the web-store brief
([`../UltraStore/WebstoreBrief.md`](../UltraStore/WebstoreBrief.md), decided
2026-11-14) both treat HTML as an unstyled "shadow" that a WebAssembly
renderer replaces. Under this proposal the HTML becomes the site itself,
and WebAssembly becomes an optional add-on. §9 asks the user to confirm the
change.

What was checked: the repository on this date (`main` at `021a094c`), with
file references to that tree. What was not: the example site could not be
fetched from this environment (its host is outside the session's network
allowance). What is said about Docusaurus comes from its published
documentation as known to the author, and numbers that could not be
measured here are marked **[unverified]**.

---

## 1. Recommendation

**Build one headless C++ library, `UltraSite`, that turns a list of routes
into a folder of static files. Each route becomes complete, styled,
readable HTML. Next to the pages go one shared, content-hashed stylesheet,
one script of about 4 KB that adds instant navigation, a search index,
the sitemap and the host's cache rules. UltraPress produces its routes
from its blocks, its theme and (new) Markdown docs. The UltraStore admin
backend produces its routes from the signed catalog it publishes. Both
apps keep their content model. What changes is that the HTML stops being
a shadow and becomes the product.**

| Decision | Reason |
|---|---|
| Every route is pre-rendered into a complete HTML file, styled by a shared CSS file (§3) | This is what makes a Docusaurus site fast. The first byte already is the page: there is no runtime to download, compile and lay out before anything shows. Search engines, link previews, screen readers, *find in page*, translation and printing all work, because the page is a document. |
| **No React, no hydration.** It is a multi-page site that feels like a single-page app (§4) | Docusaurus ships React plus its router and re-renders every page in the browser ("hydration") so that clicks feel instant [unverified: about 150–250 KB of JavaScript compressed on a typical site]. A browser gets the same instant feel cheaply: the next page is prefetched while the pointer rests on its link, the shared CSS and script are already cached, and the browser's own *speculation rules* and *view transitions* do the rest. One script, ≤ 4 KB Brotli, deferred. The site works fully with JavaScript turned off. |
| No Node.js, no webpack: the generator is C++ in the apps' own engines (§5) | UltraPress already has a headless engine that writes HTML (`UltraPressHtml.cpp`), and the admin backend already writes pages (`WebSite.cpp`, `PageFrame.cpp`). The framework has the pieces a Docusaurus build gets from npm: Markdown (`UCRichDocument::FromMarkdown` / `ToHTML`), code tokenising for highlighting (`UltraCanvasSyntaxTokenizer.h`), HTML parsing and escaping (`HTMLReader`), JSON, SHA-256 (UltraCrypt / libsodium) and image export (`UCImage`: AVIF, WebP, JPEG). Nothing new needs to be installed. |
| One shared library, not two copies (§5) | Both apps need the same pipeline: routes, layouts, hashed assets, navigation, search, sitemap, cache headers, link checks, size budgets. Both of today's emitters already repeat parts of it: `UltraPressHtml.cpp`'s head and sitemap, and `WebSite.cpp` / `PageFrame.cpp`'s frame and stylesheet. |
| UltraStore's web store becomes HTML pages generated from the catalog at publish time (§7) | The admin backend signs the catalog and writes the pages in the same step, so the pages are as trustworthy as the catalog. The browser has nothing to verify, because *Install* hands off to `ultrastore://`, and the native store checks the signature there. A first visit drops from the 9.9 MB runtime the brief measured to tens of kilobytes plus pictures. |
| Size budgets enforced by the build, as Docusaurus enforces broken links (§6) | "Fast" is a number or it drifts. The build prints the size of each page and asset and fails when one goes over its budget, or when a link or anchor inside the site is broken. |

**Do not** add Node, npm, React or a bundler to the tree. **Do not** keep
two HTML writers per app: UltraPress's shadow becomes the full page.
**Do not** load web fonts from other origins: fonts are self-hosted or
system fonts.

---

## 2. What Docusaurus does, and what we take from it

| Docusaurus | Effect | UltraSite |
|---|---|---|
| Static site generation: each route rendered to `index.html` at build time | The first paint needs nothing but HTML and CSS | **Same.** `<route>/index.html` for every route, `404.html`, and a trailing-slash policy |
| Content-hashed assets (`main.<hash>.js`, `styles.<hash>.css`) cached forever | A repeat visit and every later page fetch only the HTML | **Same.** `assets/<name>.<sha256:10>.<ext>`, `Cache-Control: public, max-age=31536000, immutable`. HTML uses `no-cache`, so it is checked again on every visit and stays current |
| React hydration and a client-side router | Navigation without a full reload | **Replaced** by prefetch plus a normal page load (§4). With the CSS cached and the HTML prefetched, a page load *is* instant, and nothing has to be restored on the client |
| Link prefetch when a link is hovered or comes into view | The next page is already there when the user clicks | **Same idea, smaller.** `<script type="speculationrules">` (Chromium prerenders, for free) plus a few lines of `ultrasite.js` that add `<link rel=prefetch>` on hover or touch in other browsers |
| Infima CSS with custom properties, and dark mode set by an inline script before first paint | No flash of the wrong theme | **Same.** CSS custom properties come from the app's theme tokens. An inline script of under 300 bytes in `<head>` sets `data-theme` from `localStorage` or `prefers-color-scheme` before paint |
| Docs layout: navbar, sidebar (generated from folders or `sidebars.js`), table of contents, breadcrumbs, previous/next, "edit this page" | Documentation sites | **Same**, as the `doc` layout (§5.3), with the sidebar generated from the docs folder or a `sidebars.json` |
| MDX: Markdown with React components | Rich pages | **UltraPress blocks** are our components. Markdown docs go through `UCRichDocument`. A fenced block can embed a registered block (`::: hero` … `:::`) [later] |
| Prism code highlighting | Readable code | **At build time**: `UltraCanvasSyntaxTokenizer` writes `<span class="tok-…">`, so the browser runs no highlighting script |
| Local search plugin, or Algolia | Searching without a server | **Local index**: one hashed JSON file per language, fetched on the first keystroke and searched in `ultrasite.js`. No outside service |
| i18n: one build per locale under `/<lang>/`, with `hreflang` | Translated sites | **Same.** UltraPress already has `Site::languages` and `Page::language`, and the catalog carries `LocalizedText` for every official language |
| Versioned docs | Docs per release | **Later** (Phase D) |
| `onBrokenLinks: 'throw'` | No dead links ship | **Same**, and anchors are checked too |
| Sitemap, robots, RSS feed | Discovery | **Already written** by `UltraPressHtml.cpp`. Moves into the library |

---

## 3. The output

```
out/
  index.html                         every route: complete HTML, styled, readable without script
  docs/getting-started/index.html
  app/org.ultraos.ultravideo/index.html
  de/…                               one tree per further language, hreflang between them
  404.html
  assets/site.3f9a1c2b7e.css         one stylesheet: tokens, layouts, blocks; hashed, immutable
  assets/ultrasite.8d2e4f0193.js     the navigation, theme and search script; deferred
  assets/img/hero.1a2b3c4d5e.1200.avif   pictures: AVIF + WebP + JPEG/PNG, widths for srcset
  search/en.5c6d7e8f90.json          the search index of one language
  sitemap.xml  robots.txt  feed.xml
  .htaccess  _headers                Apache and Netlify/Cloudflare-style cache and type rules
  *.br  *.gz                         precompressed copies beside the text files (optional)
```

Every page's `<head>` contains, in this order: the charset and viewport, the
title, the description, the canonical URL, `hreflang`, the Open Graph and
Twitter card tags, the inline theme script, the stylesheet, `modulepreload`
or a deferred script, the speculation rules and JSON-LD. In the body there
is a skip link, then `<header>` with the navbar, then `<main>`, then
`<footer>`. The LCP picture of a page gets `fetchpriority="high"` and no
`loading="lazy"`. Every other picture is `loading="lazy" decoding="async"`,
and every `<img>` carries `width` and `height`, which `UltraPressMedia.h`
already reads, so the layout never shifts.

### 3.1 Budgets (the build fails over them)

| What | Budget, Brotli | Why |
|---|---|---|
| A page's HTML | ≤ 30 KB (warning at 20 KB) | The first round trip carries the whole page |
| The stylesheet | ≤ 20 KB | One file, cached for every page |
| `ultrasite.js` | ≤ 4 KB | Optional by construction |
| Inline theme script | ≤ 0.5 KB raw | It blocks first paint |
| A search index | ≤ 150 KB per language (split per section above) | Fetched only when the visitor searches |
| Fonts | 0 by default (system font stack). A site with brand fonts: WOFF2 subsets ≤ 100 KB in all, `font-display: swap`, the title weight preloaded | Fonts are the largest render-blocking cost after CSS |
| Pictures above the fold | ≤ 300 KB (the brief's Discover rule, kept) | |
| Requests before first paint | HTML + CSS (+ one preloaded font) | |

The targets that are measured, not budgeted: Lighthouse performance and
accessibility at 100 on the mobile profile, CLS 0, and Total Blocking Time 0.
CI checks them with the Chromium that is already installed (§8).

---

## 4. Navigation: a multi-page site that feels like a single-page app

The choice is between Docusaurus's own approach (hydrate, then route on the
client) and a multi-page site with prefetch. The proposal takes the second:

1. **Speculation rules** in every page:
   `{"prerender":[{"where":{"href_matches":"/*"},"eagerness":"moderate"}]}`.
   Chromium-based browsers prerender a same-origin page while the pointer
   rests on its link, so the click shows a page that is already rendered.
   Browsers without support ignore the tag.
2. **View transitions** in the stylesheet:
   `@view-transition { navigation: auto; }`. The navbar and sidebar stay
   still while `<main>` changes, in browsers that support it. In others it
   is a normal page load, which from the cache is fast anyway.
3. **`ultrasite.js`** (deferred, ≤ 4 KB) fills in where the browser does
   not: on `pointerenter` / `touchstart` of a same-origin link it adds
   `<link rel="prefetch">`, once per URL and only on a connection that
   allows it (`navigator.connection.saveData`). It also runs the theme
   toggle and the search box.

Why not swap `<main>` with `pushState` like a router? Every page would need
its scripts re-run, its scroll position and focus restored, and its live
region announced, and that would have to be maintained for every block.
Speculation rules plus a cached page load gives the same result in
Chromium, and nearly the same elsewhere. If measurement (§8) shows Firefox
or Safari navigations above 150 ms from the cache, a `swap` mode can be
added to `ultrasite.js` behind a site setting, with focus moved to the new
`<h1>` and an `aria-live` announcement.

---

## 5. The library

`UltraSite/` at the top level of the tree, beside `UltraCloud/` and
`UltraNet/`: a static library `UltraSite`, with its documentation in
`Docs/Modules/UltraSite/README.md`. It links only the core library (JSON,
HTMLReader, RichDocument, SyntaxTokenizer, `UCImage` for the picture
variants) and UltraCrypt (SHA-256). It opens no window and builds headless,
so a CI leg or a server can publish.

### 5.1 The data

```cpp
namespace UltraSite {

struct NavItem { std::string label, href; std::vector<NavItem> children; };

struct SiteConfig {
    std::string url;                         // "https://example.org", no trailing slash
    std::string name, tagline, logo;         // logo: a source file, hashed into assets/
    std::vector<std::string> languages;      // the first is the default, at /
    bool trailingSlash = true;               // /docs/intro/ -> docs/intro/index.html
    Tokens tokens;                           // colours (light and dark), fonts, radii, widths -> CSS custom properties
    std::vector<NavItem> navbar, footer;
    std::string extraCss;                    // the app's own: block and layout rules
    Budgets budgets;                         // §3.1, overridable per site
};

struct Heading { int level; std::string text, id; };

struct Route {
    std::string path;                        // "/", "/docs/intro/", "/app/org.ultraos.ultravideo/"
    std::string language;                    // empty: the site's first
    std::string layout = "page";             // "page", "doc", "post", or an app-registered layout
    std::string title, description, image;   // head and social card
    std::string bodyHtml;                    // what goes in <main>: written by the app
    std::vector<Heading> toc;                // filled by the builder from bodyHtml's h2/h3 when empty
    std::string sidebar;                     // the id of a sidebar (doc layout)
    std::string lastModified, editUrl;
    std::vector<std::string> jsonLd;         // structured data blocks
    std::map<std::string, std::string> alternates;   // language -> path of the same page in that language
    bool noindex = false, searchable = true;
};

struct BuildResult {
    bool ok = false;
    std::vector<std::string> files, removed, errors, warnings;   // broken links and over-budget items are errors
    std::vector<std::pair<std::string, size_t>> sizesBrotli;     // what the build prints
};

class Builder {
public:
    explicit Builder(SiteConfig config);
    std::string Asset(const std::string& sourcePath);            // copies into assets/ under its hash, returns the URL
    std::string Picture(const std::string& sourcePath, const PictureOptions&);  // <picture> with srcset, sizes, width/height
    void AddSidebar(const std::string& id, std::vector<NavItem> items);
    void AddLayout(const std::string& name, LayoutFunction layout);   // an app's own page frame
    void AddRoute(Route route);
    BuildResult Build(const std::string& outputDirectory, const BuildOptions& = {});
};

} // namespace UltraSite
```

### 5.2 The build, step by step

1. Every route is laid into its layout. The library's layouts are `page`,
   `doc` and `post`; an app can register more.
2. The body gets heading ids, in the same form as Docusaurus anchors
   (`HTML::Parser` reads the body and writes it back). From these the
   table of contents and the search records are built.
3. Code blocks are highlighted (`UltraCanvasSyntaxTokenizer`).
4. Links are checked. A link to a route or anchor inside the site that
   does not exist is an error. A link from the app's content to a page it
   has dropped is caught here.
5. The CSS is built from the tokens, the layouts and the app's
   `extraCss`, minified and hashed. The script is a fixed resource,
   hashed. Search indexes are built per language.
6. Files are written. With `removeStale` (as in `PublishStatic` today),
   files an earlier build wrote and this one did not are deleted.
   Optionally `.br` / `.gz` copies are written (zlib is already a
   dependency; Brotli would be a new one, so it is optional and needs §9
   question 5 answered first).
7. The size report is printed, and every budget is checked.

### 5.3 The `doc` layout

On wide screens the layout has three columns: the sidebar, the content
(at most 46 rem wide), and the table of contents (sticky, its current
heading followed by `ultrasite.js` with an `IntersectionObserver`). Below
997 px the sidebar becomes a `<details>` drawer and the table of contents
folds into a "On this page" `<details>` above the text. This works without
script. Breadcrumbs come from the sidebar path, previous/next from the
sidebar order, and "Edit this page" from `editUrl`.

---

## 6. UltraPress on UltraSite

What changes in `Apps/UltraPress`:

| Piece | Today | With UltraSite |
|---|---|---|
| `HtmlEmitter` (`engine/UltraPressHtml.cpp`) | A whole page, unstyled, with a 1 KB built-in stylesheet | It writes the **body** of each block with classes (`up-hero`, `up-columns`, `up-card`, `up-faq` as `<details>`…). Head, sitemap, feed and robots move to UltraSite |
| `Theme` (`theme.json`) | Colours and a font family; "the HTML shadow does not use it" | Colours become `--up-accent`, `--up-ink`, … The dark look is new to the site theme (`ThemeFor(dark)` is the editor's own look, not the site's): `theme.json` gets an optional `dark` colour set, and a site without one publishes light only. The font is a system stack by default, or a font from the media folder subset to WOFF2 |
| Pages | `pages/*.json` with blocks | Unchanged |
| **Docs (new)** | — | `docs/**/*.md` with front matter (`title`, `description`, `slug`, `sidebar_position`, `sidebar_label`), read by `UCRichDocument::FromMarkdown`. The sidebar is generated from the folder tree and `_category_.json`, as in Docusaurus, or taken from `sidebars.json`. A site can have only blocks, only docs, or both |
| Posts | `kind: "post"`, in the feed | The `post` layout, plus a blog index and pages per tag [later] |
| `PublishStatic` | Shadow pages, sitemap, feed, `.htaccess` | `UltraSite::Builder::Build`, keeping the same options (`includeDrafts`, `removeStale`, `copyMedia`) and the deploy manifest (`UltraPressDeploy.h`), so *Publish* uploads only what changed. Hashed asset names make that exact |
| WebAssembly renderer | Planned as the published page | **Optional**: `--renderer` keeps working for a site that wants it, and the HTML is complete without it |
| The editor's preview | Element renderer, "pixel for pixel" the published page | Stays for editing. A new **Preview in browser** button builds into a temporary folder and opens it. The two can now differ: see §10, risk 1 |

`ultrapress publish mysite out` writes the new output. `ultrapress check`
also reports the build's broken links and budget overruns. A new
`ultrapress serve mysite` (Phase D) needs a small HTTP server on the
loopback interface, which UltraNet does not have yet. Until then, *Preview
in browser* writes the site with `trailingSlash` pages that open from
`file://`, with every internal link pointing at `…/index.html`.

**Dogfooding.** The first docs site to build with it is this repository's
own `Docs/`. It is several hundred Markdown files and already has the
`## Contents` inventories and `llms.txt`. If UltraSite can publish it under
the budgets, it can publish any customer's docs.

---

## 7. The UltraStore web store on UltraSite

What changes in `Apps/UltraStoreAdmin/core/WebSite.cpp`. The routes stay
those the brief listed:

| Route | Body, from the catalog |
|---|---|
| `/` Discover | The featured app, category chips, *Editor's picks*, *New and updated*, *Free essentials* (cards: icon, name, publisher, rating, price) |
| `/categories/`, `/category/<id>/` | The chips and a card grid. The price filter is a set of links to `?price=free` pages written at build time, or a `<form>` with `ultrasite.js` filtering in the page. Works without script either way |
| `/app/<id>/` | Header with badges, *Install* / *Try* / *Buy* (all `ultrastore://app/<id>` as today, with the store download where no store answers), screenshots as a horizontal `scroll-snap` strip of `<picture>`s, About, What's new, What it installs, information, more from the publisher; JSON-LD `SoftwareApplication` with its offer |
| `/<lang>/…` | The same pages in every official language the catalog carries (`LocalizedText`), with `hreflang` alternates; machine-translated texts are marked as the catalog marks them |
| `/search/` + index | Names, summaries, categories and publishers of every app, about 20 KB for a hundred apps [estimate] |

The look comes from `Docs/UltraStore/design/README.md` and becomes the
store's tokens and `extraCss`: ground `#FFFFFF`, ink `#0F172A`, accent
`#2563EB`, cards with a 20 px radius and a 1 px `#E2E8F0` border, pill
buttons with 44 px touch targets, a 1280 px column, and the phone layouts of
`PhoneHome` / `PhoneDetail` at narrow widths. Manrope (800) and Inter
(400–700) as WOFF2 subsets: the brief's `pyftsubset` cut already exists,
and for the web it is written as WOFF2 instead of TTF.

The pictures keep the brief's rules: AVIF + WebP + fallback, made once by
`ImportMedia`, with `srcset` widths.

**What the store no longer needs on the web:** the side module, the
runtime under `runtime/<hash>/` (9.9 MB compressed on a first visit), the
loader, the locale catalogues for `Tr` in the browser (the page is already
in the visitor's language) and the in-browser signature check. The check
moves to where it matters: the admin backend only writes pages from a
catalog it has just signed and verified, and the native store verifies
again before it installs anything. `Apps/UltraStore/web` and its CI leg stay
until the HTML store has been accepted (§9 question 1), then the
publish setting `web.site_dir` stops being needed.

The partner landing pages under `go/` (`PageFrame.cpp`) are written
through the same builder, so the store's pages and the payment service's
pages (`Server/UltraStorePay/ultrastore_pay/pages.py`) stay in one look.
The stylesheet those two share today becomes the store's `extraCss`.

---

## 8. Tests and measurement

- **`Tests/UltraSite`** (ctest), on fixture sites:
  - each route's HTML parses (`HTML::Parser`) and has exactly one `<h1>`,
    a title, a description and a canonical URL;
  - hashed names change when content changes and stay the same when it
    does not;
  - a broken link or anchor fails the build;
  - the budgets are enforced;
  - stale files are removed;
  - the sidebar, table of contents and previous/next are right for a
    nested docs folder.
- **UltraPress engine tests** (`Tests/UltraPress`): the existing shadow
  tests become body tests. The example site `Apps/UltraPress/examples/ultraos`
  builds with no warnings.
- **The admin backend's tests**: the fixture store
  (`Apps/UltraStore/web/test/fixture`) publishes to HTML, and every app
  in the catalog has a page in every language.
- **A browser test** (Playwright with the Chromium already installed): it
  serves the output folder and checks the following, failing on a number
  over its target:
  - no console errors;
  - with JavaScript disabled, every page reads and every link works;
  - the theme does not flash when dark mode is stored;
  - prefetch fires on hover;
  - CLS is 0;
  - navigation from the cache stays under 150 ms;
  - Lighthouse performance and accessibility scores, where the
    `lighthouse` package can be installed in CI.

  CI prints a *Site sizes* table, as `wasm-build.yml` prints *Demo sizes*.

---

## 9. Open questions for the user

1. **HTML first, WebAssembly optional, for both apps?** The proposal
   reverses UltraPress's "WebAssembly, HTML only as a shadow" (design
   §1, §4.4) and the web store's runtime-plus-side-module decision of
   2026-11-14. It recommends HTML as the product, with the WebAssembly
   output kept as an option for UltraPress and retired from the store
   once the HTML store is accepted.
2. **A small script.** The UltraPress brief ruled out "a JavaScript layer".
   Is a ≤ 4 KB, optional, progressive-enhancement script acceptable for
   prefetch, theme toggle and search? Without it there is no search box,
   prefetch works only in Chromium (speculation rules), and the theme
   toggle becomes a `<select>` that reloads the page.
3. **Documentation sites.** Should UltraPress publish Markdown docs (the
   Docusaurus use case, as on docs.onramp.ltd) from the first phase, with
   this repository's `Docs/` as the acceptance site? Or are block pages
   and the store first?
4. **Where the library lives.** A top-level module `UltraSite/` (proposed),
   or inside `Apps/UltraPress/engine`, with the admin backend linking the
   UltraPress engine?
5. **Precompression.** `.gz` copies need nothing new (zlib). `.br` copies
   need the Brotli library as a new dependency (`Docs/Dependencies.md`,
   `master_dependencies.yaml`, `THIRD_PARTY_LICENSES.md`). Most hosts
   compress on the fly anyway. Worth adding?
6. **Fonts.** A system font stack for UltraPress sites by default (zero
   bytes), and self-hosted WOFF2 subsets for UltraStore's Manrope / Inter?

---

## 10. Risks

1. **The editor's preview and the published page can now differ.** Today
   the element renderer is meant to be both. Under this proposal CSS draws
   the site and the elements draw the preview. Mitigation: *Preview in
   browser*, and a fixture test that compares the block order and texts
   of both. If exact parity matters more than speed, the editor can later
   show the HTML through UltraWeb's HTML reader instead of the element
   renderer.
2. **Markdown dialect.** `UCRichDocument::FromMarkdown` is written for
   documents, not for Docusaurus's MDX. Admonitions (`:::note`), tabs and
   front matter need to be added, either in the reader or in a
   pre-pass in UltraSite. The pre-pass keeps the core reader unchanged.
3. **Speculation rules are Chromium-only** [unverified for the current
   state of Firefox and Safari]. Other browsers get prefetch from the
   script, which is slightly slower than a prerender, but still served from
   the cache.
4. **Two stylesheets to keep in step for the store** (`PageFrame.cpp`'s
   and `pages.py`'s), as today. Generating `pages.py`'s copy from the
   token file at build time would remove the risk; that is outside this
   proposal.

---

## 11. Phases

| Phase | Delivers | Acceptance |
|---|---|---|
| **A** | `UltraSite` core: routes, `page` / `post` layouts, tokens → CSS, hashed assets, inline theme script, speculation rules, `ultrasite.js` (prefetch, theme), sitemap / robots / feed / 404 / `.htaccess` / `_headers`, link check, budgets, size report. UltraPress publishes through it (block bodies + theme CSS) | The `examples/ultraos` site under budget; browser test green with and without script |
| **B** | Docs: Markdown + front matter + admonitions, the `doc` layout (sidebar, table of contents, breadcrumbs, previous/next, edit link), build-time highlighting, search index + search box, one tree per language | This repository's `Docs/` published under budget; search finds a component by name |
| **C** | UltraStore: `WebSite.cpp` writes the store through UltraSite in every catalog language; `go/` pages too; WebAssembly store retired from publish after acceptance | The fixture store's pages in every language; Discover's first screen ≤ 300 KB with pictures and ≤ 60 KB without |
| **D** | Picture `srcset` variants in the builder, `.gz` / `.br`, `ultrapress serve` (loopback HTTP), versioned docs, blog tags, the optional `swap` navigation mode | As each lands |

---

## 12. Summary

Docusaurus is fast because every page is already HTML when it reaches the
browser, and the shared assets are cached for good. The React layer on top
is not what makes it fast. UltraSite copies that model in C++, inside the
engines UltraPress and the UltraStore admin backend already have:

- complete, styled pages;
- one hashed stylesheet;
- a 4 KB optional script;
- prefetch through the browser's own speculation rules;
- a local search index;
- build-time checks on links and sizes.

UltraPress gains documentation sites, and its pages become styled HTML
instead of a shadow. The web store's first visit shrinks from megabytes of
runtime to the page itself. Both decisions that change earlier ones are
questions 1 and 2 of §9.
