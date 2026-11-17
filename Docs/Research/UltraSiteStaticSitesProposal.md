# UltraSite — the HTML + JavaScript output of UltraPress and UltraStore (Investigation and Proposal)

**Date:** 2026-11-17
**Status:** Phases A to D built (2026-11-17): `UltraSite/` 0.4,
UltraPress's HTML + JavaScript output, its documentation, `ultrapress docs`
and `ultrapress serve`, and the UltraStore web store as HTML + JavaScript
(§13 to §16). The `form` module and the `swap` navigation wait (§16).
**Scope:** `UltraSite`, one shared headless library that writes the **HTML +
JavaScript** output of UltraPress (`Apps/UltraPress`) and of the UltraStore
web store (`Apps/UltraStoreAdmin`). It writes it the way
[Docusaurus](https://docusaurus.io) does: every page is rendered to its own
complete, styled HTML file when the site is built; small JavaScript modules
add instant navigation, search and the interactive parts; and the shared
assets carry a content hash so browsers cache them for good. The example
the request named is <https://docs.onramp.ltd/>.

**Decided by the user on 2026-11-17: both apps offer two outputs.**

| | (a) WebAssembly | (b) HTML + JavaScript |
|---|---|---|
| What the visitor gets | The page drawn by UltraCanvas on a `<canvas>`, compiled to WebAssembly, with an HTML shadow for crawlers | Complete, styled HTML pages, with JavaScript modules for the interactive parts |
| UltraPress | The block renderer (`Apps/UltraPress/render`) under Emscripten, as [`UltraPressDesignProposal.md`](UltraPressDesignProposal.md) §4 plans | **UltraSite**, this proposal |
| UltraStore web store | The runtime plus the side module (`Apps/UltraStore/web`), as [`../UltraStore/WebstoreBrief.md`](../UltraStore/WebstoreBrief.md) built it | **UltraSite**, this proposal |
| Status | Exists for the store; planned for UltraPress | New |

Neither option replaces the other. Both are written from the same content:
UltraPress's site folder and the store's signed catalog. The site owner (or
the store's operator) chooses per site which one *Publish* writes (§6.1,
§7.1).

What was checked: the repository on this date (`main` at `021a094c`), with
file references to that tree. What was not: the example site could not be
fetched from this environment (its host is outside the session's network
allowance). What is said about Docusaurus comes from its published
documentation as known to the author, and numbers that could not be
measured here are marked **[unverified]**.

---

## 1. Recommendation

**Build one headless C++ library, `UltraSite`, beside `UltraCloud/` and
`UltraNet/`. It turns a list of routes into a folder any static host
serves:**

- **complete HTML per route,** styled by one shared, content-hashed
  stylesheet;
- **a small set of JavaScript modules,** each hashed and loaded only by the
  pages that use it: navigation and prefetch, theme, search, and the
  behaviours of interactive blocks (tabs, carousels, filters, forms);
- **the site's files:** search index, sitemap, feed, robots, 404 and the
  host's cache rules.

**UltraPress hands it routes made from its blocks, its theme and (new)
Markdown docs. The UltraStore admin backend hands it routes made from the
catalog it has just signed. The WebAssembly output of both apps stays as
it is, and *Publish* writes whichever one the site has chosen.**

| Decision | Reason |
|---|---|
| Every route is pre-rendered into a complete HTML file, styled by a shared CSS file (§3) | This is what makes a Docusaurus site fast. The first byte already is the page: there is no runtime to download, compile and lay out before anything shows. Search engines, link previews, screen readers, *find in page*, translation and printing all work, because the page is a document. |
| JavaScript as **plain ES modules, one per behaviour**, with no framework and no hydration (§4) | Docusaurus ships React plus its router and re-renders every page in the browser ("hydration") [unverified: about 150–250 KB of JavaScript compressed on a typical site]. UltraSite's pages are already final HTML, so the JavaScript only adds behaviour to elements that are there. A page without interactive blocks loads the 4 KB core and nothing else. |
| Every behaviour enhances HTML that already works (§4.3) | Tabs without the script are stacked sections, a carousel is a scrolling strip, a filter is links to pre-built pages, and a form posts normally. A failed script, a blocked CDN or a reader with JavaScript off still gets the whole site. |
| No Node.js, no npm, no bundler in the tree (§5) | The generator is C++ in the apps' own engines. The JavaScript is hand-written source files in `UltraSite/js/`, compiled into the library as resources and hashed at build time. The framework already has everything a Docusaurus build gets from npm: Markdown (`UCRichDocument::FromMarkdown` / `ToHTML`), code tokenising (`UltraCanvasSyntaxTokenizer.h`), HTML parsing and escaping (`HTMLReader`), JSON, SHA-256 (UltraCrypt / libsodium) and image export (`UCImage`: AVIF, WebP, JPEG). |
| One library for both apps, not two copies (§5) | Both need the same pipeline: routes, layouts, hashed assets, scripts, search, sitemap, cache headers, link checks and size budgets. Both of today's HTML writers already repeat parts of it: `UltraPressHtml.cpp`'s head and sitemap, and `WebSite.cpp` / `PageFrame.cpp`'s frame and stylesheet. |
| The same blocks in both outputs, checked (§6.2) | Under option (a) a block is drawn by `UltraPressRender`; under (b) UltraSite writes its HTML and, where needed, a behaviour. A site that uses a block one output cannot show is refused by `ultrapress check` for that output, rather than published with a gap. |
| Size budgets enforced by the build, as Docusaurus enforces broken links (§3.1) | "Fast" is a number or it drifts. The build prints the size of each page, stylesheet and script and fails when one goes over its budget, or when a link or anchor inside the site is broken. |

**Do not** add Node, npm, React or a bundler. **Do not** load scripts or
fonts from other origins: everything is self-hosted, so the site works
offline from a folder and needs no third-party consent banner.

---

## 2. Choosing between the two outputs

What the site owner is told in the editor and in the documentation:

| | (a) WebAssembly | (b) HTML + JavaScript |
|---|---|---|
| First visit | Runtime plus page: 9.9 MB compressed for the store's runtime as measured by the brief, paid once per release | The page itself: tens of KB of HTML, CSS and script, plus pictures |
| Later pages | A few KB of content | A few KB of HTML (CSS and scripts cached) |
| Looks like the desktop app | Exactly: the same UltraCanvas elements draw both | Close: CSS from the same theme tokens |
| Search engines, link previews, accessibility | Through the HTML shadow | The page itself |
| Without JavaScript | The shadow only | The whole site |
| Host requirements | Static host; long-cache headers recommended | Any static host |
| Best for | App-like sites, showcases of ULTRA OS, sites that must match the desktop pixel for pixel | Content sites, documentation, shops found through search, visitors on phones |

The default for a new site is open question 1 (§9).

---

## 3. What option (b) writes

```
out/
  index.html                          every route: complete HTML, styled, readable without script
  docs/getting-started/index.html
  app/org.ultraos.ultravideo/index.html
  de/…                                one tree per further language, hreflang between them
  404.html
  assets/site.3f9a1c2b7e.css          one stylesheet: tokens, layouts, blocks; hashed, immutable
  assets/js/core.8d2e4f0193.js        navigation, prefetch, theme; on every page
  assets/js/search.1f0a9b8c7d.js      loaded on the first focus of the search box
  assets/js/tabs.5e4d3c2b1a.js        one module per behaviour, on the pages that use it
  assets/img/hero.1a2b3c4d5e.1200.avif   pictures: AVIF + WebP + JPEG/PNG, widths for srcset
  search/en.5c6d7e8f90.json           the search index of one language
  sitemap.xml  robots.txt  feed.xml
  .htaccess  _headers                 Apache and Netlify/Cloudflare-style cache and type rules
  *.gz  (*.br)                        precompressed copies beside the text files (optional)
```

Every page's `<head>` contains, in this order:

- the charset and viewport;
- the title, the description and the canonical URL;
- `hreflang`, the Open Graph and the Twitter card tags;
- the inline theme script (under 300 bytes, sets `data-theme` before first
  paint, so dark mode never flashes);
- the stylesheet;
- `<script type="module" src="core…js">` and a `modulepreload` for each
  behaviour module the page uses;
- the speculation rules and the JSON-LD.

The body is a skip link, `<header>` with the navbar, `<main>` and
`<footer>`. Pictures follow fixed rules:

- the LCP picture of a page gets `fetchpriority="high"` and is not lazy;
- every other picture is `loading="lazy" decoding="async"`;
- every `<img>` carries `width` and `height` (`UltraPressMedia.h` already
  reads them), so the layout never shifts.

### 3.1 Budgets (the build fails over them)

| What | Budget, Brotli | Why |
|---|---|---|
| A page's HTML | ≤ 30 KB (warning at 20 KB) | The first round trip carries the whole page |
| The stylesheet | ≤ 20 KB | One file, cached for every page |
| `core.js` | ≤ 4 KB | On every page |
| One behaviour module | ≤ 3 KB | Loaded only where used |
| All script a page loads before interaction | ≤ 15 KB | The Docusaurus starting point is ten times that |
| Inline theme script | ≤ 0.5 KB raw | It blocks first paint |
| A search index | ≤ 150 KB per language (split per section above) | Fetched only when the visitor searches |
| Fonts | 0 by default (system font stack). With brand fonts: WOFF2 subsets ≤ 100 KB in all, `font-display: swap`, the title weight preloaded | Fonts are the largest render-blocking cost after CSS |
| Pictures above the fold | ≤ 300 KB (the brief's Discover rule, kept) | |
| Requests before first paint | HTML + CSS (+ one preloaded font) | |

The targets that are measured, not budgeted:

- Lighthouse performance and accessibility at 100 on the mobile profile;
- CLS 0;
- Total Blocking Time 0.

CI checks them with the Chromium that is already installed (§8).

---

## 4. The JavaScript

### 4.1 How it is built and shipped

- **Source.** Hand-written ES2020 modules in `UltraSite/js/`, one file per
  behaviour, no dependencies between them except `core.js`'s small
  helpers. They target evergreen browsers from the last two years; no
  transpiling, no polyfills.
- **Into the library.** CMake turns each file into a byte array in a
  generated header, as other modules embed their resources, so the C++
  library is self-contained and needs no file next to it at run time. A
  comment-and-whitespace strip at that step is enough. Brotli does the
  rest, and a real minifier would mean Node.
- **Into the site.** The builder reads each page body for `data-us="…"`
  attributes, works out which modules the page needs, writes each module
  once under its hash and links exactly those from the page. A module
  shared by many pages is downloaded once and cached across the site.
- **An app's own behaviour.** `Builder::AddScript(name, source)` registers
  a module the same way. The store uses it for its install link (§7). A
  module goes through the same budget.
- **Data for a page** that a behaviour needs (the store's app list for its
  filter) is a `<script type="application/json" data-us-data>` island in
  the page, not a second request.

### 4.2 Navigation: a multi-page site that feels like a single-page app

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
3. **`core.js`** fills in where the browser does not. On `pointerenter` /
   `touchstart` of a same-origin link, and for links that scroll into view
   while the browser is idle, it adds `<link rel="prefetch">`. It does this
   once per URL and skips it when the visitor asked to save data
   (`navigator.connection.saveData`). It also runs the theme toggle.

Why not swap `<main>` with `pushState` like a router? Every page would need
its behaviours started again, its scroll position and focus restored and
the change announced to screen readers. All of that would have to be
maintained for every block. Speculation rules plus a cached page load gives
the same result in Chromium, and nearly the same elsewhere. If measurement
(§8) shows Firefox or Safari navigations above 150 ms from the cache, a
`swap` mode is added to `core.js` behind a site setting: it moves focus to
the new `<h1>`, announces the page in an `aria-live` region and starts the
new page's behaviours.

### 4.3 The behaviours

Each one is a module, each enhances HTML that already works, and each is
keyboard-operable and screen-reader-labelled:

| Module | Used by | Without the script | With it |
|---|---|---|---|
| `core` | every page | Plain links, light or dark from `prefers-color-scheme` | Prefetch, theme toggle remembered in `localStorage` |
| `search` | the search box | A link to `/search/`, a page listing every section | Fetches the language's index on first focus; ranked results as you type; ↑↓ Enter; `/` focuses the box |
| `toc` | `doc` layout | The table of contents as plain links | Highlights the heading in view (`IntersectionObserver`) |
| `tabs` | UltraPress *Tabs* block, code samples per language | All panels stacked under their headings | ARIA tabs; the choice remembered across the site (as Docusaurus `groupId`) |
| `carousel` | hero sliders, the store's screenshots | A horizontal `scroll-snap` strip | Arrows, dots, swipe, *pause* for autoplay, `prefers-reduced-motion` honoured |
| `lightbox` | pictures, screenshots | The link opens the full-size picture | A `<dialog>` with the picture, ←→, Esc |
| `filter` | the store's category grid, a blog's tags | Links to the filtered pages the builder wrote | Filters the cards in place from the page's data island and updates the URL (`?price=free`) |
| `form` | UltraPress *Form* and *Newsletter* blocks | A normal `POST` to the form's endpoint | Validation messages in place, `fetch` submit, the answer shown without leaving the page |
| `copy` | code blocks | Selectable text | A *Copy* button |

FAQs use `<details name="…">`, which the browser makes an accordion with no
script, and the mobile menu and docs sidebar use `<details>` too.

### 4.4 What the scripts may not do

- Load anything from another origin.
- Change the text of a page that has finished loading. They only add
  behaviour, so a crawler and a visitor without JavaScript read the same
  page.
- Track the visitor. UltraPress's SEO tracking (design §7.10) is a separate,
  opt-in module that the site owner turns on.
- Use `eval` or inline event handlers. The pages work under a strict
  `Content-Security-Policy: script-src 'self'`, with the theme script
  allowed by its hash, which the builder writes into `_headers` /
  `.htaccess`.

---

## 5. The library

`UltraSite/` at the top level of the tree: a static library `UltraSite`,
with its documentation in `Docs/Modules/UltraSite/README.md`. It links only
the core library (JSON, HTMLReader, RichDocument, SyntaxTokenizer, `UCImage`
for the picture variants) and UltraCrypt (SHA-256). It opens no window and
builds headless, so a CI leg or a server can publish.

```
UltraSite/
  include/UltraSite/
    UltraSite.h              Builder, SiteConfig, Route, BuildResult
    UltraSiteTokens.h        theme tokens -> CSS custom properties, light and dark
    UltraSiteMarkdown.h      front matter, admonitions, tabs -> body HTML (over UCRichDocument)
    UltraSiteSearch.h        the index format, written per language
  core/                      the builder, layouts, link check, budgets, the head writer
  css/                       base.css, layouts.css, blocks.css: the stylesheet's parts
  js/                        core.js, search.js, toc.js, tabs.js, carousel.js, lightbox.js,
                             filter.js, form.js, copy.js
  CMakeLists.txt             embeds css/ and js/ as resources
Tests/UltraSite/             ctest, and the browser test (js/test/)
```

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
    std::string bodyHtml;                    // what goes in <main>: written by the app, data-us="…" marks behaviours
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
    void AddScript(const std::string& behaviour, std::string source); // an app's own module, data-us="<behaviour>"
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
   table of contents and the search records are built, and the
   `data-us` behaviours the page uses are collected.
3. Code blocks are highlighted at build time (`UltraCanvasSyntaxTokenizer`
   writes `<span class="tok-…">`). The browser runs no highlighter.
4. Links are checked. A link to a route or anchor inside the site that
   does not exist is an error. A link from the app's content to a page it
   has dropped is caught here.
5. The CSS is built from the tokens, the layouts and the app's
   `extraCss`, then minified and hashed. The scripts the site uses are
   written under their hashes, and search indexes are built per language.
6. Files are written. With `removeStale` (as in `PublishStatic` today),
   files an earlier build wrote and this one did not are deleted.
   Optionally `.gz` copies are written (zlib is already a dependency) and
   `.br` copies (Brotli would be a new dependency, open question 4).
7. The size report is printed, and every budget is checked.

### 5.3 The `doc` layout

On wide screens the layout has three columns: the sidebar, the content
(at most 46 rem wide), and a sticky table of contents. Below 997 px the
sidebar becomes a `<details>` drawer and the table of contents folds into
an "On this page" `<details>` above the text. Breadcrumbs come from the
sidebar path, previous/next from the sidebar order, and "Edit this page"
from `editUrl`. All of it works without script.

### 5.4 What Docusaurus does, and what UltraSite takes

| Docusaurus | UltraSite |
|---|---|
| Each route pre-rendered to `index.html` | **Same**, plus `404.html` and a trailing-slash policy |
| Content-hashed `main.<hash>.js`, `styles.<hash>.css`, cached forever | **Same**: `assets/<name>.<sha256:10>.<ext>`, `Cache-Control: public, max-age=31536000, immutable`; HTML `no-cache` |
| React hydration, client-side router | **Replaced** by prefetch and a cached page load (§4.2) |
| Prefetch on hover and in view | **Same**, through speculation rules and `core.js` |
| Infima CSS with custom properties, dark mode without a flash | **Same**, from the app's theme tokens |
| Docs: sidebar from folders or `sidebars.js`, table of contents, breadcrumbs, previous/next, edit link | **Same** (§5.3), sidebar from the folder or `sidebars.json` |
| MDX: Markdown with React components | **UltraPress blocks** are the components; Markdown docs go through `UCRichDocument`, with `:::note` admonitions and `:::tabs` in a pre-pass |
| Prism highlighting | **At build time** |
| Local search plugin or Algolia | **Local index**, `search.js` |
| i18n: one build per locale under `/<lang>/` | **Same** |
| Versioned docs | **Later** (Phase D) |
| `onBrokenLinks: 'throw'` | **Same**, anchors too |
| Sitemap, robots, feed | **Same**, moved from `UltraPressHtml.cpp` |

---

## 6. UltraPress

### 6.1 Choosing the output

`site.json` gains `"output": "html"` or `"wasm"`. The editor shows the
choice in *Site settings…*, with §2's table as its help text.
`ultrapress publish mysite out` writes the site's choice, and
`--output html|wasm` overrides it for one run. The deploy manifest
(`UltraPressDeploy.h`) uploads only what changed either way. Hashed asset
names make that exact for (b).

### 6.2 One block set, two writers

Every block type in the registry (`UltraPressBlocks.cpp`) has three
parts:

| Part | Option (a) | Option (b) |
|---|---|---|
| Its properties | The registry, unchanged | The registry, unchanged |
| How it is shown | `UltraPressRender.cpp` builds elements | `UltraPressHtml.cpp` writes the body HTML with classes (`up-hero`, `up-columns`, `up-card`…) and, where it has one, a `data-us` behaviour |
| Its look | The theme through `PaletteFromTheme` | The theme through `UltraSite::Tokens` and the block CSS in `extraCss` |

The registry records which outputs each block type supports. `ultrapress
check` reports a block the site's output cannot show. A test renders the
example site both ways and compares the block order, texts, links and
picture `alt`s, so the two writers cannot drift on content.

### 6.3 What changes in the engine

| Piece | Today | With option (b) |
|---|---|---|
| `HtmlEmitter` (`engine/UltraPressHtml.cpp`) | A whole page, unstyled, with a 1 KB built-in stylesheet: the shadow | Writes **block bodies** for UltraSite. The shadow that option (a) needs keeps working: it becomes an UltraSite layout, `shadow`, with the renderer's loader, so both outputs share the head, sitemap, feed and robots code |
| `Theme` (`theme.json`) | Colours and a font family | Colours become `--up-accent`, `--up-ink`, … The dark look is new to the site theme (`ThemeFor(dark)` is the editor's own look, not the site's): `theme.json` gets an optional `dark` colour set, and a site without one publishes light only. The font is a system stack by default, or a font from the media folder subset to WOFF2 |
| Pages | `pages/*.json` with blocks | Unchanged |
| **Docs (new)** | — | `docs/**/*.md` with front matter (`title`, `description`, `slug`, `sidebar_position`, `sidebar_label`). The sidebar is generated from the folder tree and `_category_.json`, as in Docusaurus, or taken from `sidebars.json`. Option (a) shows them through the `richtext` block |
| Posts | `kind: "post"`, in the feed | The `post` layout, plus a blog index and pages per tag [later] |
| The editor's preview | Element renderer | Unchanged for editing; a new **Preview in browser** button builds option (b) into a temporary folder and opens it |

**Dogfooding.** The first docs site to build with option (b) is this
repository's own `Docs/`: 452 Markdown files, with the `## Contents`
inventories and `llms.txt` already in place. If UltraSite publishes it
under the budgets, it can publish any customer's docs.

---

## 7. The UltraStore web store

### 7.1 Choosing the output

The admin backend's publish setting `web.output` takes one of two values:

- **`wasm`** is today's site (`WebSite.cpp`): the shadow pages, the side
  module and the runtime from `web.site_dir`.
- **`html`** is this proposal: `WebSite.cpp` hands its routes to UltraSite
  instead.

Both are written from the catalog the same publish has just signed and
verified.

### 7.2 The pages of option (b)

| Route | Body, from the catalog | Behaviours |
|---|---|---|
| `/` Discover | The featured app, category chips, *Editor's picks*, *New and updated*, *Free essentials* (cards: icon, name, publisher, rating, price) | `carousel` for the featured row |
| `/categories/`, `/category/<id>/` | The chips and a card grid; the price filter's pages (`?price=free`) written at build time | `filter` from the page's data island |
| `/app/<id>/` | Header with badges, *Install* / *Try* / *Buy*, screenshots, About, What's new, What it installs, information, more from the publisher; JSON-LD `SoftwareApplication` with its offer | `carousel` and `lightbox` for the screenshots; the store's own `install` module (below) |
| `/<lang>/…` | The same pages in every official language the catalog carries (`LocalizedText`), with `hreflang` alternates; machine-translated texts marked as the catalog marks them | |
| `/search/` + index | Names, summaries, categories and publishers of every app, about 20 KB for a hundred apps [estimate] | `search` |

*Install*, *Try* and *Buy* open `ultrastore://app/<id>` in both options,
for the reason the brief gives: the licence must land in the native store.
The store's `install` module (≤ 1 KB, `AddScript`) does what the side
module does today. If no store answers the link within a moment, it shows
the store's download (`web.store_download_url`) in place. Without script
the download link is simply there beside the button.

The signature: under option (a) the module verifies the catalog in the
browser, because it fetches it there. Under (b) the browser receives pages,
not the catalog, so the check is done where the pages are written: the
admin backend writes them only from a catalog it has just signed and
verified. The native store checks again before it installs anything.

The look comes from `Docs/UltraStore/design/README.md` and becomes the
store's tokens and `extraCss`:

- ground `#FFFFFF`, ink `#0F172A`, accent `#2563EB`;
- cards with a 20 px radius and a 1 px `#E2E8F0` border;
- pill buttons with 44 px touch targets;
- a 1280 px column, and the phone layouts of `PhoneHome` / `PhoneDetail`
  at narrow widths;
- Manrope (800) and Inter (400–700). The brief's `pyftsubset` cut
  already exists; for the web it is written as WOFF2 instead of TTF.

`StoreTheme.h`, which the native window and option (a) share, is the
source the tokens are generated from. All three outputs then follow one
definition of the colours.

The pictures keep the brief's rules: AVIF + WebP + fallback, made once by
`ImportMedia`, with `srcset` widths. The partner landing pages under `go/`
(`PageFrame.cpp`) go through the same builder in both options, since they
are HTML today.

---

## 8. Tests and measurement

- **`Tests/UltraSite`** (ctest), on fixture sites:
  - each route's HTML parses (`HTML::Parser`) and has exactly one `<h1>`,
    a title, a description and a canonical URL;
  - hashed names change when content changes and stay the same when it
    does not;
  - a page links exactly the modules its `data-us` attributes name;
  - a broken link or anchor fails the build;
  - the budgets are enforced;
  - stale files are removed;
  - the sidebar, table of contents and previous/next are right for a
    nested docs folder.
- **The browser test** (`UltraSite/js/test/`, Playwright with the
  Chromium already installed, as the web store's `webstore-test.mjs`
  runs). It serves a fixture site that uses every behaviour and checks:
  - each behaviour by keyboard and pointer, and with JavaScript disabled
    (every page reads and every link and form works);
  - no console errors under the strict CSP;
  - the theme does not flash;
  - prefetch fires on hover;
  - CLS is 0 and a navigation from the cache stays under 150 ms;
  - Lighthouse performance and accessibility, where the `lighthouse`
    package can be installed in CI.

  CI prints a *Site sizes* table, as `wasm-build.yml` prints *Demo sizes*.
- **UltraPress** (`Tests/UltraPress`): the example site
  `Apps/UltraPress/examples/ultraos` published both ways, the content
  comparison of §6.2, and `check` refusing a block an output cannot show.
- **The admin backend**: the fixture store
  (`Apps/UltraStore/web/test/fixture`) published both ways; under
  option (b), every app has a page in every language.

---

## 9. Open questions for the user

Questions 1 and 3 were settled by the go-ahead for Phase A (2026-11-17), on
the recommended answers: HTML + JavaScript is the default output, and the
library is the top-level module `UltraSite/`.

1. ~~**The default for a new site.**~~ HTML + JavaScript (settled).
2. **Documentation sites in the first phase?** Markdown docs (the
   docs.onramp.ltd use case) with this repository's `Docs/` as the
   acceptance site in Phase B, or block pages and the store first?
3. ~~**Where the library lives.**~~ The top-level module `UltraSite/`
   (settled).
4. **Brotli copies.** `.gz` copies need nothing new (zlib). `.br` copies
   need the Brotli library as a new dependency (`Docs/Dependencies.md`,
   `master_dependencies.yaml`, `THIRD_PARTY_LICENSES.md`). Most hosts
   compress on the fly anyway. Worth adding?
5. **Fonts.** A system font stack for UltraPress sites by default (zero
   bytes), and self-hosted WOFF2 subsets for UltraStore's Manrope / Inter?

---

## 10. Risks

1. **The two outputs can drift in look.** Option (a) is drawn by
   UltraCanvas elements, option (b) by CSS. The content comparison of
   §6.2 keeps the *content* identical. The *look* is held close by
   generating both from the same tokens, and by screenshots of the example
   site in both outputs side by side in the CI artifact, which a person
   compares; no pixel test is proposed.
2. **Twice the work per new block.** Every block type needs an element
   renderer and an HTML writer, and possibly a behaviour. The registry's
   output flags (§6.2) make a block that has only one of them visible
   instead of silently missing.
3. **Markdown dialect.** `UCRichDocument::FromMarkdown` is written for
   documents, not for Docusaurus's MDX. Admonitions, tabs and front matter
   are added in a pre-pass in UltraSite, which keeps the core reader
   unchanged.
4. **Speculation rules are Chromium-only** [unverified for the current
   state of Firefox and Safari]. Other browsers get prefetch from
   `core.js`, which is slightly slower than a prerender but still served
   from the cache.
5. **JavaScript without a toolchain.** No linter or type checker runs
   over `UltraSite/js/` unless CI adds one. The browser test covers each
   behaviour, and the modules stay small enough to review whole. A
   `node --check` pass in CI (Node is already there for the web store's
   tests) catches syntax errors.

---

## 11. Phases

| Phase | Delivers | Acceptance |
|---|---|---|
| **A** | `UltraSite` core: routes, `page` / `post` / `shadow` layouts, tokens → CSS, hashed assets, inline theme script, speculation rules, `core.js`, sitemap / robots / feed / 404 / `.htaccess` / `_headers` with CSP, link check, budgets, size report. UltraPress: `"output"` in `site.json`, block bodies, theme CSS, the registry's output flags, `--output` | `examples/ultraos` published both ways; option (b) under budget; browser test green with and without script |
| **B** | Docs: Markdown + front matter + admonitions + tabs, the `doc` layout, build-time highlighting; `search`, `toc`, `tabs`, `copy`; one tree per language | This repository's `Docs/` published under budget; search finds a component by name |
| **C** | UltraStore: `web.output`; `WebSite.cpp` through UltraSite in every catalog language, with `carousel`, `lightbox`, `filter` and the store's `install`; tokens from `StoreTheme.h`; `go/` pages through the builder | The fixture store both ways; under (b) Discover's first screen ≤ 300 KB with pictures and ≤ 60 KB without |
| **D** | `form` with UltraPress's form and newsletter blocks (needs a form endpoint: the server tier of design §6.2, or a third-party one); picture `srcset` variants in the builder; `.gz` / `.br`; `ultrapress serve` (loopback HTTP); versioned docs; blog tags; the optional `swap` navigation | As each lands |

---

## 12. Summary

Both apps publish in two ways:

- **WebAssembly,** as they are designed to today;
- **HTML + JavaScript,** written by a new shared library, `UltraSite`.

UltraSite copies what makes Docusaurus fast and leaves out what makes it
heavy. It keeps every page pre-rendered as complete, styled HTML, one
hashed stylesheet cached for good, prefetch through the browser's own
speculation rules, a local search index, and build-time checks on links
and sizes. It drops React and hydration. Instead it ships small,
self-contained ES modules, one per behaviour, loaded only by the pages that
use them, and each one enhances HTML that already works without it.

The C++ engines both apps already have write the routes. UltraPress gains
documentation sites and a styled HTML output beside its WebAssembly one.
The web store gains an HTML output whose first visit is the page itself
rather than the runtime. The five questions in §9 decide the defaults and
the order.

---

## 13. What Phase A built, and what it found different

Built on 2026-11-17:
- **The library:** `UltraSite/`, reference in
  [`../Modules/UltraSite/README.md`](../Modules/UltraSite/README.md),
  tests in `Tests/UltraSite`.
- **UltraPress's side:** `Apps/UltraPress/engine/UltraPressSite.h`, plus the
  `output` setting in `site.json`, *Publish as*, and `--output`.

The example site `Apps/UltraPress/examples/ultraos`, published as HTML +
JavaScript and measured by the build (gzip -9):

| File | Compressed | Raw |
|---|---|---|
| The front page | 2.3 KB | 6.2 KB |
| The stylesheet (base, tokens, blocks) | 3.0 KB | 10.2 KB |
| `core.js` | 0.8 KB | 1.8 KB |

Checked in Chromium (Playwright) at 1280 px and 390 px, light and dark,
and with JavaScript off. There were no console errors under the page's
Content-Security-Policy, and the theme choice carried to the next page.

Where the build differs from the plan above:

- **Budgets are measured with gzip -9, not Brotli.** zlib is already a
  dependency and Brotli is not (question 4). gzip output is larger than
  Brotli's, so a page under budget here is under it on a Brotli host too.
- **The `shadow` layout is not built.** The WebAssembly output keeps
  writing its shadow through `PublishStatic`, unchanged. Its head, sitemap
  and feed moving onto UltraSite is left for when the renderer is released.
- **No behaviour module ships with the library yet.** The mechanism
  (`AddScript`, `data-us`, one hashed module per page that uses it) is
  built and tested. `search`, `toc`, `tabs`, `carousel`, `lightbox`,
  `filter`, `form` and `copy` come with Phases B to D, where their first
  users are. The UltraPress blocks need none: the FAQ is a `<details name>`
  accordion and the forms post without script.
- **Fonts are not downloaded.** The theme's family is named first in the
  stack, then the system's (question 5's recommendation for UltraPress).
- **The editor's *Preview in browser* button and `ultrapress check`
  running the link check are not built.** A publish reports both. The
  button belongs with `ultrapress serve` in Phase D.

---

## 14. What Phase B built, and what it found different

Built on 2026-11-17:

- **The library.** Documentation sites in UltraSite 0.2: `AddDocs`, the
  `doc` layout, `UltraSiteMarkdown.h`, the search index and page, the
  `search`, `toc`, `tabs` and `copy` modules, the language switcher, and
  `Labels`. See the *Documentation sites* section of the module README.
- **The framework.** The `headingId` and `codeBlock` hooks of
  `RichDocumentHTMLOptions`.
- **UltraPress.** A site's `docs/` and `i18n/<language>/docs`, and the
  `ultrapress docs` command.

This repository's own `Docs/` was the acceptance site. It was published
with `ultrapress docs`, leaving out the changelogs:

| What | Result |
|---|---|
| Pages | 418, plus generated index pages for the folders without one |
| Build time | 18 s in a debug build on four cores |
| The stylesheet | 3.7 KB compressed |
| `core.js` | 0.8 KB compressed |
| `search.js` | 2.0 KB compressed |
| The largest other module | 0.8 KB compressed |
| A typical page | 3 KB to 15 KB compressed |
| The search index | 125 KB compressed (titles, headings and descriptions of 7 156 sections) |

Checked in Chromium (Playwright), with no console errors:

- searching by keyboard finds *UltraClipboard* first for "Clipboard";
- arrow and Enter open the result;
- the table of contents follows the scroll;
- the *Menu* link opens the sidebar on a phone;
- without JavaScript the search page lists all 418 pages.

What the acceptance site showed, and what was done about it:

- **The framework's Markdown reader takes a line as a paragraph.** It is
  the editor's format. A web page's Markdown wraps paragraphs, as
  CommonMark does. UltraSite joins the wrapped lines of paragraphs, list
  items and quotes before handing the text over, and leaves the reader
  alone, since Texter's round trip depends on it.
- **Heading ids must be GitHub's.** The `.md` links a repository already
  has name anchors the way GitHub spells them. GitHub drops punctuation,
  dashes included, and keeps letters in any script. `Slugify` does the
  same.
- **The full search index was 906 KB compressed**, six times its budget,
  and the section texts made up 2.1 MB of its 3.1 MB. An index over budget
  is now written again with titles, headings and descriptions only, and
  the build says so. A small site, which every UltraPress site so far is,
  keeps full-text search.
- **The whole sidebar on every page made long pages heavy.** With 470
  entries it cost about 7 KB compressed per page. Now only the categories
  on the way to the page are expanded, as Docusaurus does with collapsed
  ones, and the others link to their generated pages.
- **Parsing every page three times cost two thirds of the build.** Each
  page is now read back once, which finds its behaviours, its ids and its
  links in one pass. The build went from 32 s to 18 s.
- **Over budget, and correctly so.** Ten of the repository's documents
  are larger than 30 KB compressed on their own, `UltraCanvasFilerWidget.md`
  86 KB. The budget reports them, and `--no-budgets` publishes anyway.
- **Real broken links.** The repository's docs have 68 links to `.md`
  files that do not exist (`UltraCanvasUIElement.md`,
  `UltraCanvasButton.md` and others) and 55 anchors that no heading has,
  most of them spelled with an em dash that GitHub's ids leave out. The build fails on them, as Docusaurus
  does; `--broken-links warn` publishes anyway.

Not built, and left for later:

- versioned docs (Phase D, as planned);
- Markdown's lists inside quotes, which the framework's reader does not
  read;
- reference-style links (`[text][ref]`);
- the WebAssembly output showing the docs.

---

## 15. What Phase C built, and what it found different

Built on 2026-11-17:

- **The store.** `Apps/UltraStoreAdmin/core/WebStoreHtml.h`. The admin
  backend's publish writes the web store through UltraSite when
  `web.output` is `html`.
- **The default.** Unset, a store that names `web.site_dir` keeps the
  WebAssembly store it had, and any other gets the HTML one. `off` writes
  none.
- **Pages.** Discover; the categories, with their free and paid lists as
  pages; each category; each app.
- **Languages.** One tree per language of the apps' texts. Their words
  come from the store's own translations, with a navbar into that
  language's pages and a language switcher.
- **Search, markup, actions.** A search box, `SoftwareApplication` JSON-LD
  for each app, and *Install* / *Try* / *Buy* as `ultrastore://` links.
  The store's own `install` module points at the download when no store
  answers the link.
- **UltraSite 0.3.** A site under a path, `Builder::Url`, a frame per
  language, and the folder's own files as link targets when nothing is
  swept.

The acceptance site is the web store's browser-test fixture
(`Apps/UltraStore/web/test/fixture`, nine apps, English and German),
published again as HTML. The admin tests check every listed app's page in
both languages, and that the publish gives no warning.

| Discover, first visit | Measured | Budget (§7) |
|---|---|---|
| Without its pictures (page, stylesheet, scripts; gzip -9) | 11 KB | 60 KB |
| Its pictures on the first screen (1280 × 800, Chromium) | 47 KB | 300 KB |

Checked in Chromium (Playwright), with no console errors under the
policy:

- the German pages' navbar leads into the German pages;
- searching "video" finds UltraVideo;
- the free list works with JavaScript off.

Where the build differs from the plan above:

- **The catalog lives under a path** (`https://store.ultraos.eu/v1`), and
  the WebAssembly store's pages used relative links for it. UltraSite
  learned a site path, as Docusaurus has `baseUrl`, rather than writing
  relative links. A page then reads the same at any depth, and the link
  check still resolves every address.
- **The publish folder is shared.** It holds the catalog, the media, the
  packages and the partner pages. So the store's build sweeps nothing, and
  links into the folder's own files count. A missing picture, a broken
  link or a page over budget is a warning in the publish report, never a
  failed publish, because the catalog matters more than a page.
- **No `filter` module.** The price filter is three pages written when
  the site is built (all, `free/`, `paid/`). That works without script
  and is prefetched like any page.
- **No `carousel` or `lightbox`.** Screenshots are a scroll-snapped strip
  of links to the full pictures, which needs no script.
- **The tokens repeat `StoreTheme.h`'s values.** That header belongs to
  the window and pulls in the UI, which the headless admin core cannot
  include. Both name each other.
- **Fonts are not downloaded.** Manrope and Inter are named first and the
  system's fonts follow (question 5).
- **The partner pages stay where they were.** The `go/` and `join/` pages
  keep their own frame (`PageFrame.cpp`), shared with the payment service.

---

## 16. What Phase D built, and what waits

Built on 2026-11-17:

- **Responsive pictures.** `Builder::Picture` writes AVIF and WebP
  variants at the widths below a picture's own, plus its own, named by
  content hash and reused by a later build. The original is the `<img>`,
  with its size. UltraPress gives every media picture its role's widths
  and `sizes` (the hero, a card, a picture block).
- **Precompressed copies.** `BuildOptions::precompress` writes `.gz`
  copies (zlib) and `.br` copies (Brotli, optional). They are for nginx's
  `gzip_static` / `brotli_static` and Caddy's `precompressed`.
  `ultrapress publish --precompress` and `ultrapress docs --precompress`
  ask for them.
- **The preview server.** `UltraSiteServer.h` serves a built folder on
  127.0.0.1, the way a host would: the site's path, the trailing-slash
  redirect, the 404 page, the cache rules and the compressed copies.
  `ultrapress serve <site>` builds the site, serves it, and builds it
  again before answering a page when a file of the site changed. It also
  serves a built folder as it is.
- **Versioned docs.** `DocsOptions::versions` gives each earlier version
  its own prefix and sidebar, a version switcher, and a banner leading to
  the latest; the search finds only the latest. In UltraPress the
  versions are `versioned_docs/<label>/` with `versions.json`, and in
  `ultrapress docs` `--versions <dir>`.
- **The blog.** A post's `"tags"`, `/blog/` (the posts, newest first) and
  `/blog/tags/<tag>/`.

What the build found:

- **This machine's image library writes no AVIF.** libvips reads it, but
  libheif here has no AV1 encoder ("heifsave: Unsupported compression").
  Writing a `<source type="image/avif">` for files that would never exist
  would break the page. So `Picture` tries each format once, on a 16-pixel
  copy of the first picture, lists only what it can write, and says what
  it left out. CI's runners will tell whether theirs write AVIF.
- **A file time's zero is not "long ago".** `ultrapress serve` first
  compared modification times against a zero `file_time_type`, but
  libstdc++'s file clock counts from 2174, so every real time is negative
  and no change was ever seen. It starts from `file_time_type::min()`.

What waits, and why:

- **The `form` module.** UltraPress's forms post to `/api/form/<id>` and
  `/api/newsletter/subscribe`, which a static host does not have. The
  module is only worth writing together with the endpoint: the server
  tier (`UltraPressDesignProposal.md` §6.2) or a third-party form service
  chosen by the site. Until then the forms post normally, as they always
  have.
- **The `swap` navigation.** §4.2 made it conditional on Firefox or
  Safari navigating more slowly than 150 ms from the cache. Only Chromium
  is installed here, which prerenders through the speculation rules
  anyway, so the condition could not be measured. Without that number the
  added complexity (re-running behaviours, restoring focus and scroll) is
  not justified.
- **Responsive pictures in Markdown docs.** `UCRichDocument::ToHTML`
  writes the `<img>` itself. A picture hook like `codeBlock` would bring
  them in.

