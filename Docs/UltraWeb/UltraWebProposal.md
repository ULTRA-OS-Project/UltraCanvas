# UltraWeb — a Web Browser on the HTMLReader

**Status:** Proposal — nothing implemented. This document is the feasibility
study, the architecture and the phase plan; no code, no registry entry in
[`Masterfile_modules.md`](../../Masterfile_modules.md) and no changelog until
Phase 1 lands.
**Author:** UltraCanvas Framework / ULTRA OS
**Last Modified:** 2026-10-01

The question this document answers: *can we build a web browser, UltraWeb, on
the HTML reader the framework already has?*

**Short answer: yes, as a native reader-class browser. Pages load over
UltraNet, render as real UltraCanvas elements through the HTMLReader and the
CSSLayout engine, and links, images, stylesheets, history, tabs, downloads and
(in Phase 3) forms all work. It does not run JavaScript, so it is not a
replacement for Chromium or Firefox: script-built sites (single-page apps,
most web mail, most dashboards) show their `<noscript>` content or nothing
useful. Adding a script engine is possible later but is a separate project,
§7.**

| Capability | Phase | Basis |
|---|---|---|
| Fetch pages over HTTP/HTTPS, redirects, TLS verified | 1 | UltraNet, exists |
| Render HTML + CSS (block, inline, tables, images, backgrounds, `@media`) | 1 | HTMLReader + CSSLayout, exists |
| Relative URLs, `<base>`, charset, linked CSS, image cache | 1 | **new** page loader, §4.1 |
| Address bar, back / forward / reload / stop, `#fragment` jumps | 1 | **new** app + web view element, §4.2–4.3 |
| Downloads | 1 | `UltraNet_HttpDownloadFile`, exists |
| Flex, grid, `position`, `float`, `vw`/`vh`, `calc()` | 2 | CSSLayout supports the layouts; the resolver does not map them, §5.2 |
| Inline SVG | 2 | SVG plugin, exists |
| Working forms (GET / POST), cookies, tabs, bookmarks, find | 3 | framework elements + UltraNet sessions |
| JavaScript | 4 (optional) | not in the repository, §7 |

---

## 1. What this is, and what it is not

UltraWeb is an **application** (`Apps/UltraWeb`) plus **two reusable framework
pieces** it is built on:

- `HTML::PageLoader` — turns a URL into a ready-to-build `HTML::Document` and a
  resource loader for its subresources. Framework-independent, testable
  offline.
- `UltraCanvasWebView` — an element that shows one page: loading, building,
  scrolling, link activation, history. The browser is a window of tabs, each
  holding one web view.

Both live in the framework rather than the app because there are already two
other callers that need them. UltraMail's message preview resolves remote
images by hand (`Apps/UltraMail/ui/UltraMailMessagePreview.cpp`), and the
eBook viewer (`UltraCanvas/core/UltraCanvasEBookViewer.cpp`) re-implements
link and anchor handling. A third copy inside a browser is the hand-rolled
pattern `AGENTS.md` forbids.

It is **not** a wrapper around a system browser engine (WebKitGTK, WebView2,
WKWebView). That option is weighed in §8 and kept only as a possible later
"open in full engine" fallback.

---

## 2. What already exists

The HTMLReader module (`UltraCanvas/{include,core}/HTMLReader/`, about 5,000
lines) is the core of the browser already. It does not lay pages out itself:
it builds an element tree, and the CSSLayout engine lays it out. That is the
reason a browser is feasible at all — there is no second layout engine to
write.

| Piece | File | What it gives the browser |
|---|---|---|
| Parser | `HTMLParser.h` | Tolerant HTML/XHTML: unclosed `<p>`/`<li>`, void elements, CDATA, entities, raw-text `<script>`/`<style>`. Not the HTML5 tree-construction algorithm (§5.1). |
| DOM | `HTMLDocument.h` | `Node`, `Document`, `GetElementById`, `<title>`, `<meta>`, `<style>` blocks and `<link rel=stylesheet>` hrefs collected. |
| CSS | `CSSStyleSheet.h`, `HTMLStyleResolver.h` | Cascade with specificity and source order, inline `style=""`, user-agent defaults per tag, `@media` min/max-width, `margin: auto`, `max-width`, background images, border radius, `border-collapse`, `white-space`. |
| Builder | `HTMLElementBuilder.h` | Containers for blocks, `UltraCanvasLabel` runs with Pango markup for inline text, images flowing in text, tables on the CSSLayout table engine, inline-block boxes, a map of `id`/`name` anchors. |
| Hooks | `HTML::BuildOptions` | `resourceLoader` (images and linked stylesheets as bytes), `onLinkActivated` (raw href of the clicked link), `viewportWidth` (for `@media`), `userCss` (reader overrides). |
| Network | `UltraNet/UltraNetHttp.h` | Sync and async (`UltraNet_HttpRequestAsync`, curl_multi worker) requests, streaming chunks, progress callbacks, redirects (`maxRedirects = 10`), TLS verification on, `finalUrl` after redirects. |
| Sessions | `UltraNet/UltraNetCookies.h` | `UltraNet_CreateSession`, session GET/POST, `UltraNet_SessionLoadCookies` / `SaveCookies`. |
| URLs | `UltraNet/UltraNetUrl.h` | Parse, build, encode, query strings. **No relative-reference resolution** (§4.1). |
| UI | `UltraCanvasTabbedContainer`, `UltraCanvasTextInput`, `UltraCanvasMenu`, `UltraCanvasProgressDialog`, `UltraCanvasDropdown`, `UltraCanvasCheckbox`, `UltraCanvasRadio`, `UltraCanvasScrollbar` | The browser frame and, later, real form controls. |
| SVG | `Plugins/SVG/UltraCanvasSVGPlugin.cpp` | Vector rendering for `<img src=*.svg>` and inline `<svg>` (§5.4). |

Two consumers prove the reader on real-world markup today: the eBook engines
(EPUB, FB2, MOBI) and UltraMail's HTML message view, which handles
table-heavy marketing mail, remote images behind a consent bar and
`@media` queries at the pane width.

---

## 3. Architecture

```
 Apps/UltraWeb ─────────────────────────────────────────────────────────────
   window: toolbar (back / forward / reload / address bar / menu)
           UltraCanvasTabbedContainer ── one UltraCanvasWebView per tab
           status bar (link under pointer, load progress)
           history, bookmarks, downloads, settings (app-owned storage)
 ───────────────────────────────────────────────────────────────────────────
 UltraCanvasWebView  (UltraCanvas/{include,core}, new element)
   Navigate(url) ─► PageLoader ─► ElementBuilder ─► scroll content
   history stack, anchors, rebuild on width change, link / hover events
 ───────────────────────────────────────────────────────────────────────────
 HTML::PageLoader  (UltraCanvas/{include,core}/HTMLReader, new)
   fetch document, decode charset, resolve URLs, collect + fetch CSS,
   resource cache, cancellation
        │ fetch interface (injectable — tests use a fixture map)
 ───────────────────────────────────────────────────────────────────────────
 UltraNet   HTTP(S), session cookies, downloads, + UltraNet_ResolveUrl (new)
 HTMLReader Parser, StyleResolver, ElementBuilder  (extended in Phase 2)
 CSSLayout  block / inline / flex / grid / absolute / table
```

Data flow for one navigation:

1. `WebView::Navigate("https://example.org/a/b.html")` cancels any load in
   flight and asks the `PageLoader` for the document.
2. The loader fetches it asynchronously, takes the base URL from
   `response.finalUrl` (after redirects), decodes the body to UTF-8, parses
   it, applies `<base href>`, and fetches every `<link rel=stylesheet>` (and
   `@import`) in parallel.
3. On the UI thread the view runs `ElementBuilder::BuildDocument` with a
   `resourceLoader` that answers from the cache, and swaps the new tree in.
4. Images not yet cached are fetched in the background; when a batch arrives
   the view rebuilds (or, once §6.2 lands, patches only the affected image
   elements).
5. A click arrives through `onLinkActivated(rawHref)`. The view resolves it
   against the base: `#frag` scrolls to `anchors[frag]`, same-document links
   do not reload, `http(s):` navigates, `mailto:` and unknown schemes go to
   the host (UltraWeb hands them to the operating system).

All network work happens off the UI thread; the builder runs on the UI thread
because it creates elements. Every callback carries a navigation id so a slow
response from a page the user already left is dropped.

---

## 4. New pieces

### 4.1 `HTML::PageLoader`

```cpp
namespace UltraCanvas::HTML {

struct FetchResult {
    int status = 0;
    std::string finalUrl;          // after redirects; becomes the base URL
    std::string contentType;       // "text/html; charset=..."
    std::vector<uint8_t> body;
    std::string error;             // empty on success
};

// Injected so the loader has no UltraNet dependency and tests run offline.
using Fetcher = std::function<void(const std::string& url,
                                   std::function<void(FetchResult)> done)>;

struct LoadedPage {
    std::string url;               // final URL
    std::string baseUrl;           // url, or <base href> resolved against it
    Document document;             // parsed, linked stylesheets inlined
    std::vector<std::string> warnings;
};

class PageLoader {
public:
    explicit PageLoader(Fetcher fetcher);
    // Returns a navigation id; Cancel(id) drops its callbacks.
    int Load(const std::string& url, std::function<void(LoadedPage)> onReady);
    void Cancel(int navigationId);
    // For BuildOptions::resourceLoader: resolved against the page base,
    // answered from the cache, empty (and queued for fetch) on a miss.
    std::vector<uint8_t> GetResource(const std::string& baseUrl,
                                     const std::string& href);
    std::function<void()> onResourcesArrived;
};

}
```

What it adds that nothing has today:

- **URL resolution.** RFC 3986 §5 relative-reference resolution — `../`,
  `//host/path`, `?query`, `#fragment`, dot-segment removal. This belongs in
  UltraNet as `UltraNet_ResolveUrl(base, reference)` next to
  `UltraNet_ParseUrl`; the eBook engine's `EPUBEngine::ResolveHref` handles
  archive paths only and stays as it is.
- **Charset.** Order: BOM, then HTTP `Content-Type` charset, then
  `<meta charset>` / `<meta http-equiv=content-type>` in the first 1024 bytes,
  then UTF-8 with a Windows-1252 fallback for invalid sequences. UltraNetMime
  already converts mail bodies to UTF-8 internally (iconv when
  `ULTRANET_HAS_ICONV`); that conversion is exposed as a public
  `UltraNet_ConvertToUtf8(bytes, charset)` rather than written a second time.
- **Stylesheets.** `Document::styleSheetLinks` are fetched and their text
  inserted in document order; `@import` inside them is followed (depth limit
  4). Relative `url()` inside a stylesheet resolves against the stylesheet's
  URL, not the page's — the resolver needs to carry that base per sheet.
- **Cache.** An in-memory LRU of subresources keyed by absolute URL (byte
  budget, default 64 MB), honouring `Cache-Control: no-store`. No disk cache
  in Phase 1.
- **Content types.** `text/html` and `application/xhtml+xml` are pages;
  `text/plain` is wrapped in `<pre>`; images open as a page holding one image;
  anything else is offered as a download.
- **`file://`.** Loads from disk through `OpenFileUtf8` with paths converted
  by `PathFromUtf8` (UTF-8 path rule, `AGENTS.md`), so local HTML and
  documentation trees work without a server.

### 4.2 `UltraCanvasWebView`

A container element that owns one page.

```cpp
class UltraCanvasWebView : public UltraCanvasContainer {
public:
    void Navigate(const std::string& url);
    void Reload();
    void Stop();
    bool CanGoBack() const;  void GoBack();
    bool CanGoForward() const; void GoForward();
    bool ScrollToFragment(const std::string& id);
    void SetZoom(float factor);            // scales ResolverOptions base font
    void SetUserCss(const std::string& css);
    void SetImagesEnabled(bool enabled);

    std::string GetUrl() const;
    std::string GetTitle() const;
    bool IsLoading() const;

    std::function<void(const std::string& url)> onUrlChanged;
    std::function<void(const std::string& title)> onTitleChanged;
    std::function<void(bool loading, float progress)> onLoadStateChanged;
    std::function<void(const std::string& url)> onLinkHovered;     // status bar
    std::function<bool(const std::string& url)> onExternalScheme;  // mailto:, tel:, ...
    std::function<void(const std::string& url,
                       const std::string& suggestedName)> onDownloadRequested;
};
```

Behaviour:

- **History** entries hold URL, title and scroll offset; back/forward restore
  the offset. Fragment-only navigation pushes an entry without a reload.
- **Width changes** rebuild the page with the new `viewportWidth` so `@media`
  rules re-apply, debounced (200 ms) and only when the width crosses a
  breakpoint any loaded sheet actually uses. The eBook viewer already
  rebuilds on resize (`RebuildChapterContent`); the web view follows the same
  path.
- **Error pages** (DNS failure, TLS error, 4xx/5xx with an empty body) are
  built from a small built-in HTML template through the same builder.
- The view is the one place link activation is interpreted, so UltraMail and
  the eBook viewer can later delegate to it.

### 4.3 `Apps/UltraWeb`

Structured like the other apps (`Apps/Texter/main.cpp`): an
`UltraCanvasApplication`, one main window, its own changelog
`Docs/UltraWeb/CHANGELOG.md` and `ULTRAWEB_VERSION` per the versioning rules
in `AGENTS.md`.

- Toolbar: back, forward, reload/stop, address bar (`UltraCanvasTextInput`),
  menu. Text that is not a URL goes to a configurable search URL template.
- Tabs on `UltraCanvasTabbedContainer`; middle-click and Ctrl+click open a
  link in a new tab.
- Status bar: hovered link, load progress.
- Keyboard: Ctrl+L, Ctrl+T, Ctrl+W, Ctrl+R / F5, Alt+Left / Alt+Right,
  Ctrl+F (Phase 3), Ctrl+plus / minus / 0 for zoom.
- Downloads to the user's Downloads folder through
  `UltraNet_HttpDownloadFile`, names sanitised and kept UTF-8.
- History and bookmarks in the app's settings directory (JSON through
  `UltraCanvasJSON`).

---

## 5. Gaps in the HTMLReader, and what to do about each

Ordered by how much of the web each one costs today.

### 5.1 Parser

The parser is tolerant but not spec-conformant. Real sites rely on HTML5
tree construction for misnested formatting (`<b><p>x</b></p>`), implied
`<tbody>`, `<table>` foster-parenting and `<template>`. Plan: add the
cheap, high-yield rules (implied `tbody`/`tr`, auto-closing `<p>` before
block starts, the adoption-agency case for `<a>`/`<b>`/`<i>`) and test them
against fixtures. Replacing the parser with a vendored HTML5 parser (e.g.
lexbor, gumbo) is the alternative; it would be wrapped behind
`HTML::Parser`, per the wrapped-engines rule, and decided only if fixtures
show the hand-written rules are not enough.

`<noscript>` content must be **rendered** (there is no script), and
`<script>`, `<template>` hidden. `<iframe>` becomes a link box showing its
`src` in Phase 1.

### 5.2 Layout properties (the biggest gap)

`HTMLStyleResolver.cpp:483` maps `display: flex` and `display: grid` to
`Block`, and `float`, `position`, `z-index` are ignored
(`HTMLStyleResolver.cpp:757`). Most current sites are built from flex and
grid, so they render as one long column. The CSSLayout engine already
implements flex, grid and absolute layout (`core/CSSLayout/FlexLayout.cpp`,
`GridLayout.cpp`, `AbsoluteLayout.cpp`), so this is mapping work, not new
layout code:

| CSS | Maps to |
|---|---|
| `display: flex / inline-flex`, `flex-direction`, `flex-wrap`, `justify-content`, `align-items`, `align-self`, `gap`, `flex: g s b`, `order` | `layout.SetFlexRow/Column`, `layoutItem.SetFlexGrow/Shrink/Basis/AlignSelf` |
| `display: grid`, `grid-template-columns/rows` (px, %, `fr`, `repeat()`, `minmax()`), `grid-column/row`, `gap` | CSSLayout grid |
| `position: absolute / relative / fixed / sticky`, `top/right/bottom/left`, `z-index` | absolute layout; `fixed` relative to the view; `sticky` as `relative` in Phase 2 |
| `float: left/right`, `clear` | the builder's existing image-float path, generalised to boxes |
| `vw`, `vh`, `vmin`, `vmax`, `calc()`, `min()`, `max()`, `clamp()` | resolver length evaluation, with the viewport size passed in `ResolverOptions` |
| `overflow: hidden / auto` | clip / nested scroll container |
| `visibility`, `opacity`, `box-shadow`, `text-transform`, `letter-spacing`, `text-overflow` | element properties that exist or are small |

The builder currently wraps runs of inline content into labels inside block
containers; flex and grid items must become direct children of the flex/grid
container instead (anonymous items for loose text, as in CSS). This is the
one structural change in the builder.

### 5.3 Selectors

The stylesheet parser skips rules whose selectors use unsupported syntax
(most pseudo-classes). Add `:first-child`, `:last-child`, `:nth-child()`,
`:not()`, `:root`, attribute selectors with operators, and `:hover` /
`:focus` (re-resolving only the affected subtree on pointer change). Unknown
selectors keep being skipped rather than matching too much.

### 5.4 Media

- `<img>` with SVG sources and inline `<svg>` through the SVG plugin
  (today only the raster `<image>` inside an EPUB cover `<svg>` is shown).
- `srcset` / `<picture>`: pick the candidate for the view width and scale.
- Animated GIF/WebP through `UCImageAnimationController`.
- `<video>` / `<audio>`: poster plus a link to open the file in UltraViewer;
  real playback is out of scope.
- Web fonts (`@font-face`): ignored in Phase 1–3; text falls back to the
  family list's generic family.

### 5.5 Forms (Phase 3)

`ElementBuilder::BuildFormControl` draws controls display-only. For a browser
they must be the framework's real elements — `UltraCanvasTextInput`,
`UltraCanvasButton`, `UltraCanvasDropdown`, `UltraCanvasCheckbox`,
`UltraCanvasRadio`, a multi-line text element — so caret, selection,
clipboard, IME and focus work (the *Build UI out of UltraCanvas elements*
rule). A `FormController` per `<form>` collects name/value pairs on submit and
sends `application/x-www-form-urlencoded` GET or POST (multipart for file
inputs) through the page loader. Password fields are masked and never logged.
The display-only path stays for UltraMail, behind a `BuildOptions` flag.

### 5.6 Performance

Every block becomes an element and the whole tree is rebuilt on each load,
image batch and breakpoint change. That is fine for articles and docs; long
news front pages (tens of thousands of nodes) will be slow. In order of cost:
coalesce image-arrival rebuilds; patch image elements in place instead of
rebuilding; build lazily below the fold. Measure first — Phase 1 records
build time and element count per page in a debug overlay.

---

## 6. Security and privacy

- **TLS verification on**, always; `acceptInvalidCert` is never set. A TLS
  failure shows an error page with the certificate details and no "proceed
  anyway" button in Phase 1.
- **No script means no script attack surface.** The remaining risks are in
  parsing and decoding: the parser, CSS parser and image decoders take
  hostile input. Fuzz targets for `HTML::Parser` and `CSSStyleSheet` are
  part of Phase 1.
- **Resource limits:** maximum document size (default 16 MB), maximum
  subresource count and total bytes per page, `@import` depth 4, redirect
  limit 10, element-count cap with a "page truncated" warning.
- **Mixed content:** an `https` page does not load `http` subresources.
- **Schemes:** only `http`, `https`, `file` (address bar only — a web page
  cannot link to `file://`), `data:` for images, and `about:`. Everything
  else is handed to the operating system after confirmation.
- **Cookies** are per-profile through an UltraNet session, first-party only
  by default, cleared from the settings page. They are off in Phase 1 and on
  in Phase 3, when forms make them useful.
- **Downloads** never auto-open; names are sanitised against path traversal.

---

## 7. JavaScript

There is no script engine in the repository, and a browser without one
cannot run most of the current web. Adding one is a project of its own:

1. Vendor and wrap a small engine (QuickJS is the natural candidate: compact,
   modern ECMAScript, MIT licence) behind an UltraCanvas-owned API.
2. Give the DOM a live, mutable model with bindings — `document`,
   `Element`, events, `querySelector`, `fetch`/`XMLHttpRequest`, timers.
3. Make the builder incremental: a DOM mutation must update the affected
   elements, not rebuild the page.

Step 3 is the hard one: today the build is one-way and one-shot. This
proposal does not commit to any of it. Phases 1–3 are designed so it could be
added later — the DOM stays the single source of truth and the web view owns
the rebuild — but they are worth having on their own.

---

## 8. Alternative considered: wrap a system web view

WebKitGTK on Linux, WebView2 on Windows, WKWebView on macOS would give full
web compatibility, JavaScript included, in a few weeks. Rejected as the
primary path because:

- the page would be an opaque native surface, not UltraCanvas elements — no
  theming, no reuse in UltraMail or the eBook viewer, no ULTRA OS or
  WebAssembly target;
- three different engines with three behaviours and three dependency stories
  (`Docs/Dependencies.md`);
- it builds nothing the framework keeps.

It remains a reasonable later addition behind the same `UltraCanvasWebView`
API — an "open in full engine" action for pages the native renderer cannot
show.

---

## 9. Phases

### Phase 1 — reader browser

- `UltraNet_ResolveUrl`, public `UltraNet_ConvertToUtf8`.
- `HTML::PageLoader` with injectable fetcher, charset detection, linked and
  `@import` stylesheets, per-sheet base URLs, LRU cache, cancellation.
- `UltraCanvasWebView`: navigate, history with scroll restore, fragments,
  rebuild on breakpoint change, error pages, zoom, link hover.
- Parser: render `<noscript>`, hide `<template>`, `<iframe>` as a link box.
- `Apps/UltraWeb`: toolbar, address bar with search fallback, single page
  view (tabs may follow in Phase 3), downloads, history file.
- Tests (§10); fuzz targets for parser and CSS parser.

**Done when:** Wikipedia articles, the repository's own `Docs/` rendered from
`file://`, typical blogs and plain documentation sites load, render readably,
and every link, image and back/forward action works, on Linux, Windows and
macOS.

### Phase 2 — modern layout

- Flex, grid, position, float, viewport units and `calc()` mapped onto
  CSSLayout (§5.2); the selector additions (§5.3).
- SVG images and inline SVG; `srcset` / `<picture>`.
- Parser rules for implied `tbody`, `<p>` auto-close and misnested
  formatting.
- Coalesced image rebuilds; image patch in place.

**Done when:** a fixed set of reference pages (a news front page, a
documentation site built on flex/grid, a product page) lay out in columns
recognisably close to a mainstream browser, checked with screenshot tests.

### Phase 3 — interactive

- Real form controls and submission (§5.5), cookies through UltraNet
  sessions, tabs, bookmarks, find in page, `:hover`/`:focus` styles.
- UltraMail and the eBook viewer move their link handling onto the web view
  or the page loader.

**Done when:** searching on a search engine's HTML results page, logging into
a site with a plain HTML login form, and keeping several tabs open all work.

### Phase 4 — optional

JavaScript (§7) or a system web view fallback (§8), decided after Phase 3 on
evidence of which sites users actually need.

---

## 10. Testing

- **Offline by construction:** the page loader takes a `Fetcher`; tests pass
  a map of URL → fixture bytes, so no test touches the network.
- **Unit:** URL resolution (the RFC 3986 §5.4 examples verbatim), charset
  detection order, stylesheet ordering and per-sheet base URLs, history and
  fragment navigation, cancellation of a stale load.
- **Layout:** extend the existing `Tests/HTMLTableLayoutTest.cpp` /
  `HTMLImageAlignTest.cpp` pattern to flex, grid and position — build a
  fixture and assert element bounds.
- **Screenshots:** the Phase 2 reference pages, stored as fixtures, rendered
  headless and compared with a tolerance.
- **Fuzzing:** parser and CSS parser.
- **Regression for the other consumers:** the eBook and UltraMail tests must
  stay green after every HTMLReader change — the browser work must not cost
  the reader its existing users.

---

## 11. Risks

| Risk | Mitigation |
|---|---|
| Users expect a full browser and meet script-only sites | Say so in the UI: a "this page needs JavaScript" notice when `<noscript>` or an empty body plus scripts is detected, with an "open in system browser" action. |
| Rebuild cost on large pages | Measure in Phase 1; incremental image patching and lazy building in Phase 2. |
| Flex/grid mapping changes how existing mail and eBooks render | Gate new layout mapping behind `ResolverOptions` until the eBook and UltraMail suites are reviewed; then turn it on for all. |
| Parser divergence from HTML5 | Fixture-driven rules; a vendored HTML5 parser behind `HTML::Parser` if fixtures demand it. |
| Hostile input in decoders | Resource limits, fuzzing, no script. |

---

## 12. Open questions

1. Does the page loader live under `HTMLReader/` (as proposed) or become its
   own small module next to UltraNet?
2. Default search provider for the address bar, and whether ULTRA OS ships a
   start page.
3. Should Phase 1 already ship tabs, or a single view with tabs in Phase 3 as
   planned?
4. Profile storage: plain files in the settings directory, or UltraVault for
   cookies once Phase 3 adds them?
