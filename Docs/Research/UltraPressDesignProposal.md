# UltraPress — a website and shop builder that publishes WebAssembly (Investigation and Proposal)

**Date:** 2026-10-12
**Status:** Proposal — investigation only, no implementation yet
**Scope:** a new application, `Apps/UltraPress` ("WorldPress 2.0" in the
brief): a WordPress-class website and online-shop system built on
UltraCanvas, whose published output is **WebAssembly rendered by the
framework's own drawing pipeline, not HTML and JavaScript**. The document
decides what "generate WebAssembly instead of HTML and JS" can mean, picks
one meaning, maps the twelve features of the brief onto what the repository
already has, names the two framework pieces that are missing, and lays out
phases and the questions only the user can answer.

This document is written in the style of the other investigations in this
directory — [`UltraCalendarDesignProposal.md`](UltraCalendarDesignProposal.md)
and [`UltraFIBUDesignProposal.md`](UltraFIBUDesignProposal.md) are the closest
siblings in shape. Everything said about the repository was verified against
the tree on this date (`main` at 0.9.261); file references are to that tree.
Everything said about browsers, search engines and payment providers is
from their published behaviour as known to the author; what could not be
checked from this environment is marked **[unverified]** rather than
guessed. One thing was measured, on this date, by the `wasm-build.yml`
workflow's new *Demo sizes* step: what a visitor downloads for the front
page demo (§4.3). Time to first paint, and the leaner builds, are Phase 0.

---

## 1. Recommendation

**Build UltraPress as a desktop application that edits a site as data and
publishes it as one WebAssembly renderer plus the site's content — not as a
site compiled into WebAssembly. The renderer is the UltraCanvas framework
itself, built once per release with the existing Emscripten port
(`UltraCanvas/OS/WASM`), the way the ultraos.eu front page is already built
today. A visitor's browser downloads the renderer once and caches it; every
page after that is a few kilobytes of content. The same content, through
the same C++ rendering code, also builds (a) the editor's live preview on
the desktop, (b) a native UltraWeb app for ULTRA OS, and (c) a plain HTML
shadow of every page for search engines, mail clients and screen readers,
which is the one place HTML stays. Dynamic features — shop, members,
newsletter, downloads, forms — run on a headless UltraPress server written
on UltraNet and UltraDatabase, which needs the HTTP server UltraNet does
not have yet.**

Why these are the right calls, in one line each; the rest of the document
is the evidence:

| Decision | Reason |
|---|---|
| One pre-built renderer `.wasm` + content as data, not one `.wasm` per site (§4) | The Emscripten build compiles cairo, pango, glib, fontconfig, libvips and the fonts into the module. That is a download of 3.9 MB Brotli-compressed (11.2 MB raw) for the front page demo, measured on this date (§4.3), and a cross-compile of minutes. Paying it once per framework release is fine; paying it on every "Publish" click, and making every visitor download a different copy per site, is not. WordPress separates theme code from content for the same reason. |
| The renderer is the ultraos.eu front page, generalised (§3.1) | `UltraCanvas/OS/WASM/demo/ultraos-frontpage.cpp` already is a website as UltraCanvas elements in a browser: navigation with popup menus, hero, card rows, forms, FAQ accordion, footer. It is hand-written C++ per page; UltraPress is the same thing driven by a page description. |
| An HTML shadow of every page, emitted from the same content (§4.4) | Google, Bing, link previews, mail clients, screen readers and `curl` read HTML. A canvas is invisible to all of them. "SEO tracking" (feature 6) and the newsletter (feature 10) cannot exist without an HTML emitter, so the emitter is built anyway; using it as the no-WebAssembly fallback is free. |
| Native UltraWeb output through the element ABI, as a second target (§4.5) | On ULTRA OS, a site should be a UltraWeb app: kilobytes, the host's theme, fonts and accessibility, no cairo in the download. The content model and block renderer are the same; only the element factory differs. It waits on UltraWeb's generated C++ proxies, which that plan lists as next. |
| A headless engine + a thin app, like UltraMail and UltraSocial (§5) | `Apps/UltraPress/engine` holds the content model, the block renderer, the HTML emitter, the importer and the publisher, and builds without a display. The editor, the server and the WebAssembly renderer are three consumers of it. |
| Two hosting tiers: static first, server second (§6) | A pure website is files: upload them with the FTP / SFTP / WebDAV providers UltraCloud already has, to any host. Shop, members, newsletter and forms need a process on a server, and that process needs an HTTP server module in UltraNet (§6.2), which is the largest piece of new framework code in this plan. |
| Hosted checkout pages for payments, never card data in UltraPress (§7.5) | Stripe Checkout, PayPal and Mollie redirect the buyer to the provider's page and call back a webhook. UltraPress then stays outside the card-data scope (PCI SAQ-A) [unverified]. Keys live in UltraVault. Orders export to UltraFIBU, which already writes XRechnung invoices. |
| Import WordPress sites from their export file (§7.1) | A WordPress WXR export is XML with HTML bodies; the repository has an XML parser (TinyXML2) and `ImportHTMLToRichDocument`. The first migrated site is ultraos.eu itself, whose front page is already in the tree. |

**Do not** compile each site to its own `.wasm`, **do not** write a JavaScript
layer around the renderer (the brief rules it out, and nothing below needs
it beyond Emscripten's own loader), and **do not** build a payment or
e-mail stack of our own: the SMTP plugin, UltraMail's outbox pattern and the
providers' hosted pages exist.

---

## 2. What was asked, restated

The brief (`UltraPress_WorldPress_2.0.txt`) asks for:

1. An application, **UltraPress**, offering what WordPress offers, built on
   UltraCanvas, that is **user-friendly, well structured and flexible**.
2. Its purpose: **websites and online shops**.
3. Twelve features: a website-builder assistant, a page and menu manager, a
   page editor with predefined blocks, theme management, a plugin shop,
   payments, SEO tracking, e-mail services, a plugin community section, a
   member section, a newsletter designer and a download manager; a page
   list that can pin pages to the top and sort them alphabetically or by
   popularity.
4. **It should generate WebAssembly code instead of HTML and JavaScript.**

Point 4 decides the architecture, and it has three possible meanings that
lead to three different products. §4 lays them out and picks one.

What WordPress is, for the comparison: a PHP program on a web server that
keeps pages, posts, menus, users and orders in MySQL, renders each page to
HTML on every request through a *theme* (PHP templates), extends itself
through *plugins* (PHP code loaded into the same process), and offers an
editor in the browser. Its weaknesses are the ones the brief's aims answer:
every plugin runs with the site's full rights, the editor and the theme
drift apart, and the whole is held together by JavaScript.

---

## 3. What the repository already provides — and what it does not

### 3.1 A website as UltraCanvas elements in a browser exists

`UltraCanvas/OS/WASM/demo/ultraos-frontpage.cpp` (1,081 lines, version 2.0.0
of 2026-10-12) is the https://ultraos.eu front page built out of framework
elements: the header with drop-down navigation, hero, desktop and mobile
cards, reasons, newsletter form, status, two pre-order offers, donation
links, social networks, variants, editions, an FAQ accordion, a contact
form and the footer. It is laid out by the CSS layout engine (Block for
stacked sections, Flex rows for cards and navigation), scrolls in a
container that fills the viewport, and opens every link with `OpenURL()`.
The texts sit in tables at the top of the file. The same source builds as a
desktop program (`-DBUILD_WASM_DEMOS=ON`) for checking the page before the
wasm pipeline runs; its only `__EMSCRIPTEN__` branch is the `Maximize()`
that fills the browser viewport.

That is UltraPress's rendering model, proven: **a page is a tree of
UltraCanvas elements, and the browser shows the framework's own pixels**.
What the demo lacks is exactly what UltraPress adds: the page is C++, not
data; there is one page, not a site; there is no editor, no theme, no
server.

The port it runs on, `UltraCanvas/OS/WASM` (`README.md` there,
`Docs/UltraCanvas/UltraCanvasWebAssembly.md`):

| Concern | State |
|---|---|
| Rendering | the same Cairo + Pango context as every desktop platform; frames go to a `<canvas>` with `putImageData`; HiDPI from `devicePixelRatio` |
| Input | mouse, wheel, touch (as left-button mouse), keyboard, focus through Emscripten's HTML5 callbacks |
| Build | Emscripten 6.0.11; a cross-compiled sysroot of cairo, pixman, pango, fontconfig, freetype, harfbuzz, glib, libvips (`build-wasm-sysroot.sh`, the better part of an hour, cached in CI); the manually dispatched `wasm-build.yml` |
| Threads | `-pthread`, a pool of 8 Web Workers; so the page must be served with the cross-origin isolation headers (`COOP: same-origin`, `COEP: require-corp`) |
| Browser services | `UltraCanvasWASMSupport.h`: IDBFS persistent storage, `FetchAsync`, downloads, `PickFilesAsync`, `localStorage`, image and font loading, the current URL, query parameters and hash |
| Not yet | IME composition, drag and drop, custom cursors, mouse capture outside the canvas; Firefox and Safari untested; the per-frame upload is the full surface |

Two of those rows matter for a public website and are taken up in §4.3:
the isolation headers, and the size of what a visitor downloads.

### 3.2 Usable as-is

| Need | What exists | Where |
|---|---|---|
| Rich text in the editor, and WordPress post bodies in | `UltraCanvasRichTextEdit` on `UCRichDocument`: styles, lists, tables, pictures, comments, tracked changes; `ImportHTMLToRichDocument` / `UCRichDocument::ToHTML` | `Docs/UltraCanvas/UltraCanvasRichTextEdit.md`, `UltraCanvasHTMLReader.md` |
| Markdown content | the Markdown plugin element | `include/Plugins/Text/UltraCanvasMarkdown.h` |
| Reading HTML and CSS (themes imported from existing sites, mail templates) | the HTMLReader module: parser, DOM, cascade, `ElementBuilder` | `Docs/UltraCanvas/UltraCanvasHTMLReader.md` |
| Layout of a page | the CSS layout engine: block, flex, grid, tables, floats | `Docs/CSSLayout.md` |
| The editor's UI: trees, breadcrumbs, pagination, forms, wizards, pickers | ~60 elements plus ~70 plugin elements — `UltraCanvasTreeView`, `UltraCanvasColumnsTreeView`, `UltraCanvasBreadcrumb`, `UltraCanvasPagination`, `UltraCanvasFormLayout`, `UltraCanvasStepper`, `UltraCanvasColorPicker`, `UltraCanvasAlbum` for media | `Docs/UltraCanvas/UltraCanvasUIElements.md` |
| Site statistics, shop dashboards | the Charts plugin elements | `include/Plugins/Charts/` |
| QR codes on invoices and tickets | the QRCode plugin | `Docs/UltraCanvas/UltraCanvasQRCodeExamples.md` |
| Site, page and theme files | `UltraCanvasJSON` (wrapping yyjson) | `Docs/UltraCanvas/UltraCanvasJSON.md` |
| Local and server storage | UltraDatabase: SQLite core, PostgreSQL driver, migrations, parameter binding only; UltraFIBU's multi-user mode runs on it | `Docs/Modules/UltraDatabase/README.md` |
| HTTP client, TLS, DNS, OAuth2 with PKCE, cookies, WebSocket, SSE | UltraNet | `Docs/Modules/UltraNet/README.md` |
| Sending mail | the SMTP plugin (`Plugins/UltraNet/smtp`) and UltraMail's persistent outbox (`Apps/UltraMail/engine/UltraMailOutbox.h`), which retries and survives restarts | |
| Receiving bounces and replies | the IMAP, POP3 and JMAP plugins | `Plugins/UltraNet/` |
| Uploading a static site to any host | UltraCloud's FTP / FTPS / SFTP / WebDAV providers, Nextcloud, Dropbox, Google Drive, OneDrive | `Docs/Modules/UltraCloud/README.md` |
| Secrets: API keys, SMTP passwords, payment keys | UltraVault; hashing, key derivation, authenticated encryption in UltraCrypt | `Docs/Modules/UltraVault/`, `Docs/Modules/UltraCrypt/` |
| One-time passwords for member two-factor sign-in | `Apps/UltraAuthenticator/otp/UltraOtp.h` (TOTP) | |
| Package files for themes and plugins | VirtualFS: `.ucpkg` bundles with Zstd sections, named for the ULTRA Store | `Docs/Modules/VirtualFS/README.md` |
| Element plugins loaded on demand, with descriptors and named properties | `UltraCanvasElementRegistry`, `UCElementDescriptor`, the DSO loader | `Docs/UltraCanvas/UltraCanvasElementPlugins.md` |
| The website-builder assistant | UltraAI `ITextLLM` with structured output and tool calls, cloud or local, and a mock adapter for tests | `UltraAI/README.md` |
| Invoices, VAT, bookkeeping for the shop | UltraFIBU: XRechnung, SKR03, GoBD journal | `Docs/Research/UltraFIBUDesignProposal.md` |
| A sandbox for third-party WebAssembly on the desktop and in UltraWeb | WasmHost (wasmtime, memory cap, call time limit), the element ABI | `Masterfile_modules.md` §17, `Docs/UltraWeb/UltraWeb.md` |
| An XML parser for WordPress exports | TinyXML2, vendored | `Docs/Dependencies.md` |

### 3.3 Not there, and needed

1. **An HTTP server.** UltraNet is a client library. It has
   `UltraNet_TcpListen` / `UltraNet_TcpAccept`, and the OAuth2 loopback
   responder in `UltraNetOAuth2.cpp` writes one `HTTP/1.1` status line
   for the browser redirect — that is the whole of the repository's server
   side. Every dynamic feature of the brief (forms, members, shop,
   downloads with counts, newsletter sign-up, "most popular") needs a
   process that answers requests. §6.2 proposes `UltraNet_HttpServe*`.
2. **A site theme model.** The framework has no theme system: no header
   under `include/` names a theme (the matches for "theme" are chart and
   diagram palettes). A website theme — colours, type scale, spacing,
   block style variants, header and footer layouts — is UltraPress's own
   data, §7.4.
3. **A browser-side History API** - added on this date: `WASMURL::PushUrl`,
   `ReplaceUrl`, `GetPath` and `SetOnNavigate` (`popstate`) in
   `UltraCanvasWASMSupport.h`, so a multi-page site in one renderer can
   update the address bar and answer the back button, §4.3.
4. **A single-threaded WebAssembly build, or a measured decision to keep
   threads.** The core propagates `-pthread` to every Emscripten
   executable, and threads force the cross-origin isolation headers on the
   serving host, §4.3.
5. **C++ element proxies for UltraWeb guests.** The element ABI today has
   five kinds (Container, Label, Button, TextInput, Checkbox); the
   generated proxies that let one C++ source build for the desktop and for
   UltraWeb are listed as next in `UltraWebProposal.md` §10. The
   UltraWeb-native output (§4.5) waits on them.
6. **A WordPress importer**, §7.1, and an HTML *emitter* for pages (the
   repository reads HTML through one module; writing a page is new,
   `UCRichDocument::ToHTML` covers the rich-text part), §4.4.

---

## 4. "WebAssembly instead of HTML and JavaScript": three meanings, one choice

### 4.1 The three products

| | A. Compile each site | B. One renderer + content as data | C. UltraWeb-native app |
|---|---|---|---|
| What "Publish" produces | a `.wasm` per site (or per page), built with Emscripten from generated C++ | the site's content, menus and theme as JSON and media files; the renderer `.wasm` ships with the UltraPress release | a `wasm32-wasip1` reactor that builds the site from UltraCanvas elements the browser owns |
| Who renders | the framework inside the module, pixels to a `<canvas>` | the same | UltraWeb's own elements, native |
| Visitor download | the whole framework + libraries + fonts, per site | the renderer once, cached across every UltraPress site on the same release; then kilobytes per page | kilobytes |
| Publish time | an Emscripten build: minutes, and a toolchain on the publishing machine | writing files: instant | writing files |
| Plugins | compiled in | data-driven blocks; code plugins as further modules (§7.3) | further wasi modules in the same sandbox |
| Where it runs | every mainstream browser | every mainstream browser | UltraWeb on ULTRA OS, Linux today |
| Search engines, link previews, mail clients, screen readers | see nothing | see nothing | UltraWeb has no crawler to satisfy; accessibility is the host's |

**A is the literal reading and the wrong product.** Its cost is paid on
every publish and by every visitor, and a site owner with a shared web host
cannot run Emscripten. It survives only as a later "export as a standalone
application" feature, which is the desktop build the demo already has.

**B is the WordPress model transposed**: the theme engine is code, the site
is data, and the two meet at page-load time — in the visitor's browser
instead of on the server. It is what the ultraos.eu demo would become if
its tables of texts were read from a file. It is the recommendation for the
open web.

**C is the ULTRA OS-native result** and is the same product seen through a
different element factory. It is built second, from the same engine.

### 4.2 Why B is still "no HTML and JavaScript"

The visitor loads one HTML file that holds nothing but the `<canvas>` and
the Emscripten loader script the port already generates. No page content,
no layout, no styling and no behaviour is in HTML, CSS or JavaScript: the
content is JSON, the layout is the CSS layout engine inside the module, the
styling is the theme the module reads, and the behaviour is C++. That is
the line the brief draws, and the port draws it in the same place today.

### 4.3 What B costs, honestly, and what Phase 0 measures

| Cost | What it means for a website | Answer |
|---|---|---|
| **Download size** | The module carries cairo, pango, glib, fontconfig, harfbuzz, freetype, libvips and the bundled Ubuntu fonts. Measured on 2026-10-12 by `wasm-build.yml`'s *Demo sizes* step (emsdk 6.0.11, `-O2`, with threads), the ultraos.eu front page demo is: `ultraos-frontpage.wasm` 6.52 MB raw, 2.59 MB gzip, 2.05 MB Brotli; `.data` (the Ubuntu fonts and three pictures) 4.57 MB raw, 2.46 MB gzip, 1.79 MB Brotli; the loader `.js` 0.15 MB raw, 0.03 MB Brotli. **11.24 MB raw, 3.9 MB over Brotli, 5.1 MB over gzip.** The minimal `wasm-demo` is 10.01 MB raw (5.69 MB of module), so about 90% of the front page's weight is the framework and its libraries, not the page. | Once per release, cached by content hash, 3.9 MB is a large picture, not a disqualifying download; on a slow mobile connection it is seconds before the first paint, which the HTML shadow covers. Phase 0 tries the levers: a build without libvips (pictures decoded by the browser through `WASMBrowser::LoadImage`), a font subset instead of the full Ubuntu family (the `.data` is 40% of the total), `-Os`, and `wasm-opt`; and measures time to first paint on a mid-range laptop and a phone. A build without libvips (pictures decoded by the browser, handed in as pixels through `WASMBrowser::LoadImage`) and with a subset of the fonts is the first thing to try. The renderer is served with a one-year cache and a content hash in its name, so it is paid once per release. |
| **Cross-origin isolation** | Threads need `SharedArrayBuffer`, which needs `COOP: same-origin` and `COEP: require-corp`. Under `require-corp` the page cannot embed a cross-origin resource that does not opt in — a payment provider's iframe, a YouTube embed, a map. | Build the renderer **single-threaded** if the framework allows it (the core passes `-pthread` PUBLIC; what inside the framework needs a worker on a website is the question Phase 0 answers). If it does not, keep threads and use hosted checkout *redirects* instead of embedded widgets (§7.5), and no third-party embeds. Either way, a static host that cannot set headers (many cannot) is served the single-threaded build or nothing. |
| **Search engines, previews, accessibility** | A canvas has no text. Google indexes nothing, a shared link shows no title and picture, a screen reader announces "canvas", the browser's find-in-page and translate do nothing, printing prints a bitmap. | The HTML shadow, §4.4. |
| **Navigation** | One module shows many pages, so the URL must change on a click and the back button must work, or the site is one URL. | Done: `WASMURL::PushUrl` / `ReplaceUrl` and `SetOnNavigate` (`popstate`) in `UltraCanvasWASMSupport.h`. The renderer reads `GetPath()` on start and on every navigation callback and shows that page. |
| **First paint** | The module must download, compile and lay out before anything shows. WordPress shows text while images are still loading. | The HTML shadow shows the page's text at once; the renderer takes the viewport over when it is ready. Browsers cache compiled WebAssembly across visits. |
| **Text selection, copy, right-click** | The framework's own, inside the canvas, not the browser's. | Acceptable for a site; the editor side is the desktop app anyway. |
| **Firefox, Safari, mobile** | Untested by the port. | Phase 0 runs the demo on all three and lists what breaks; touch is synthesised already. |

### 4.4 The HTML shadow

Every published page is two files: `page.json` for the renderer and a
plain `index.html`:

- `<title>`, `<meta name="description">`, Open Graph and Twitter card
  tags, canonical URL, `hreflang` where the site has languages;
- the page's text content in semantic order — headings, paragraphs,
  lists, links, image `alt` texts, tables — with no styling beyond a
  short built-in stylesheet so it reads as a document;
- JSON-LD structured data where a block carries it (an article, a
  product with its price, an organisation, an FAQ);
- the `<canvas>` and the loader for the renderer, which replaces the
  document's body when it has started.

That one file serves four readers: the crawler and the link preview, the
visitor whose browser has no WebAssembly or who turned it off, the screen
reader (until the renderer mirrors its element tree into hidden DOM, which
is a later task), and UltraWeb's own HTML reader, which shows the shadow
natively before the UltraWeb-native output exists. `sitemap.xml`,
`robots.txt` and the RSS feed of a blog are emitted beside it.

The emitter is also the newsletter's writer: mail clients run neither
WebAssembly nor scripts, so a newsletter *is* HTML, and it is the same
blocks through the same emitter with the e-mail-safe subset of layouts
(tables and inline styles). `UCRichDocument::ToHTML` writes the rich-text
block today.

Is the shadow "HTML the brief forbade"? It carries no layout, no styling of
the site's own and no script, and no visitor with WebAssembly sees it. The
alternative — a site that no search engine, no social network and no mail
client can read — is not a product a shop owner can use. The decision is
the user's; it is open question 1 in §11.

### 4.5 The UltraWeb-native output

The block renderer is one C++ implementation with an element factory
behind an interface. On the desktop and under Emscripten the factory makes
real elements (`CreateButton`, `std::make_shared<UltraCanvasLabel>`); for
UltraWeb it makes the generated proxies over `uc_create` / `uc_set_text`,
once `UltraWebProposal.md` §4.2's generator exists. The published site then
carries a third artefact, `site.wasm`, a `wasm32-wasip1` reactor, reached
through `<link rel="ultraweb-app">` in the shadow page as that plan
specifies. The site's content is fetched by the app through `uc_fetch` from
its own origin, under the fetch rules UltraWeb already enforces.

What this buys on ULTRA OS: no cairo in the download, the host's fonts,
theme, IME and accessibility, a 2-second call limit and a memory cap around
every site, and a site that cannot crash the browser. It is the showcase
for both products and should follow the web output by one phase, not be
left for last.

---

## 5. Architecture

```
 Apps/UltraPress/
 ├── engine/      headless: content model, block renderer (element factory
 │                behind an interface), HTML emitter, theme, importer (WXR),
 │                publisher (files → UltraCloud provider or server upload),
 │                site statistics reader. Builds and tests without a display.
 ├── ui/          the desktop application: site manager, page and menu
 │                manager, block editor with the live preview (the renderer
 │                on the desktop), theme editor, newsletter designer,
 │                member / order / download views, the assistant
 ├── server/      ultrapress-server: a command-line process on UltraNet's
 │                HTTP server + UltraDatabase; serves the site, forms,
 │                members, shop, downloads, newsletter, statistics
 ├── renderer/    the WebAssembly renderer: engine + OS/WASM, built with
 │                Emscripten per release; and the wasi reactor for UltraWeb
 └── plugins/     the plugin SDK and the bundled block packs

 published site (static tier)           published site (server tier)
 ├── index.html (shadow) + page.json     the same files, served by
 ├── <slug>/index.html + page.json       ultrapress-server, plus
 ├── site.json, menus.json, theme.json   /api/... for forms, members, shop,
 ├── media/…                             downloads, newsletter, statistics
 ├── ultrapress-<version>.wasm/.js
 ├── site.wasm (UltraWeb)
 └── sitemap.xml, robots.txt, feed.xml
```

The split follows UltraMail (engine library + GUI), UltraSocial (engine +
connectors + app) and UltraFIBU (engine, cli, report, ui): the engine is a
library target, the three consumers link it, and the tests run the engine
against fixtures. The renderer under Emscripten links the engine and the
core exactly as the demo links the core today
(`UltraCanvas/OS/WASM/demo/CMakeLists.txt`, `uc_wasm_demo`).

### 5.1 The content model

A site is a folder — plain files, so it can be versioned, copied, backed up
with UltraCloud and diffed:

```
site/
├── site.json        name, languages, domain, owner, publishing target, settings
├── theme.json       the theme (§7.4), or a reference to an installed one
├── menus.json       named menus of items: page, external link, submenu
├── pages/<slug>.json
├── posts/<slug>.json
├── products/<sku>.json
├── media/           pictures, downloads, with a media.json index (alt, title, licence)
└── plugins/         installed block packs
```

A page:

```json
{
  "slug": "the-plan", "title": "The development plan", "status": "published",
  "pinned": true, "template": "default", "language": "en",
  "seo": { "description": "…", "image": "media/plan.png", "noindex": false },
  "blocks": [
    { "type": "hero", "props": { "kicker": "Introducing ULTRA OS", "title": "…" } },
    { "type": "columns", "props": { "count": 3 }, "children": [ … ] },
    { "type": "richtext", "props": { "document": "<UCRichDocument as JSON>" } },
    { "type": "faq", "props": { "items": [ { "q": "…", "a": "…" } ] } }
  ]
}
```

Every block type is a `UCElementDescriptor`-style description: a name, its
properties with types and defaults, which child blocks it takes, and the
renderer that turns it into elements. The predefined blocks of feature 2
are the sections of the ultraos.eu demo generalised: header, hero, text,
rich text, Markdown, picture, gallery, columns, cards, feature list, call
to action, FAQ accordion, pricing, team, testimonial, contact form,
newsletter form, download list, product grid, product, cart, map
placeholder, footer. The demo's 31 element-creation sites become the
renderers of about a dozen of them.

The **page list** of the brief: `pinned` pages sort first; alphabetic sort
is the title; "most popular" is the view count the server reports (§6.2)
or, on the static tier, the count the site owner imports from their host's
log. Visibility: published, draft, scheduled (a date), members only (§7.6).

### 5.2 The block renderer

```cpp
namespace UltraPress {

// One renderer, three element factories: desktop / Emscripten (real
// elements), UltraWeb (generated proxies), and a recording factory for
// tests, which lists what would have been created.
class IElementFactory {
public:
    virtual ~IElementFactory() = default;
    virtual ElementHandle Create(const std::string& kind, const Properties& props) = 0;
    virtual void Insert(ElementHandle parent, ElementHandle child) = 0;
    virtual void Listen(ElementHandle element, Event event, std::function<void(EventDetail)> handler) = 0;
};

struct RenderedPage {
    ElementHandle root;
    std::vector<std::string> warnings;   // an unknown block type, a missing picture
};

class BlockRenderer {
public:
    BlockRenderer(IElementFactory& factory, const Theme& theme, const BlockRegistry& blocks);
    RenderedPage Render(const Page& page, const Site& site, float viewportWidth);
};

// The shadow page, the newsletter, the feed.
class HtmlEmitter {
public:
    std::string Page(const Page& page, const Site& site, const Theme& theme);
    std::string Newsletter(const Newsletter& issue, const Site& site);
    std::string Sitemap(const Site& site);
};

}
```

The desktop editor renders the page being edited with the real factory
into its preview pane; because the Emscripten build uses the same Cairo and
Pango pipeline, **the preview is the published page**, pixel for pixel at
the same width. That is the WYSIWYG WordPress never had, and it costs no
code. The editor adds selection handles and property panes around the
rendered elements; it does not re-implement them (the house rule in
`AGENTS.md`, *Build UI out of UltraCanvas elements*).

---

## 6. Hosting: two tiers

### 6.1 Static tier — any web host, today's infrastructure

"Publish" writes the folder of §5 and uploads it through UltraCloud —
FTP / FTPS / SFTP for the classic web hosts, WebDAV and Nextcloud for the
rest, or a local folder for a host that syncs itself. A static site is
pages, menus, pictures, downloads, the shadow and the renderer; a contact
form on this tier posts to a third-party form endpoint, or is left out.
This tier needs no server work and is what Phase 1 ships; it is enough for
ultraos.eu's content pages.

Constraint to state plainly: a host that cannot set response headers
serves only the single-threaded renderer (§4.3). The publisher writes an
`.htaccess` for Apache hosts with the caching, compression and, where
threads are kept, the isolation headers; nginx and Caddy snippets go into
the documentation.

### 6.2 Server tier — `ultrapress-server`

Everything dynamic runs here: forms, members and sign-in, the shop and
its webhooks, download counting and gating, newsletter subscriptions and
sending, view counts, search. It is a headless process (like UltraFIBU's
`cli` and its multi-user server mode) on UltraDatabase — SQLite on a small
host, PostgreSQL where several instances or a bookkeeping link need it —
and it needs **an HTTP server in UltraNet**, which is the main framework
addition of this proposal:

```cpp
// UltraNet/UltraNetHttpServer.h — proposed. Follows the module's rules:
// names readable as they stand, UltraNetResult for blocking calls,
// UltraNetHandle for the listener, the same option/callback shapes as the
// client side.
struct UltraNetHttpServerOptions {
    std::string bindAddress = "127.0.0.1";
    uint16_t port = 8080;
    size_t maxRequestBodyBytes = 16u << 20;
    int keepAliveSeconds = 15;
    int workerThreads = 4;
    UltraNetTlsOptions tls;              // optional; a reverse proxy usually terminates TLS
};
struct UltraNetHttpServerRequest  { std::string method, path, query; std::vector<UltraNetHeader> headers; std::vector<uint8_t> body; std::string remoteAddress; };
struct UltraNetHttpServerResponse { int status = 200; std::vector<UltraNetHeader> headers; std::vector<uint8_t> body; };
using UltraNetHttpHandler = std::function<UltraNetHttpServerResponse(const UltraNetHttpServerRequest&)>;

UltraNetHandle UltraNet_HttpServe(const UltraNetHttpServerOptions& options, UltraNetHttpHandler handler);
UltraNetResult UltraNet_HttpServeStop(UltraNetHandle server);
```

It is the first server of the stack, and not only UltraPress's: UltraWeb
has no server side (its fetch and storage are the visitor's), UltraFIBU's
multi-user mode is a shared PostgreSQL, not a service, and the ULTRA OS
cloud that `UltraCalendarDesignProposal.md` §8 and UltraCloud's roadmap
name — a CalDAV / CardDAV / WebDAV server behind one ULTRA account — needs
exactly this module underneath. Build it once, in UltraNet, and both
products are handlers on it.

HTTP/1.1 with keep-alive and chunked bodies, on the existing TCP and TLS
layers; no HTTP/2 and no TLS termination of its own in the first version —
the documented deployment puts Caddy or nginx in front for certificates
(Let's Encrypt) and HTTP/2, the same way most PHP sites run behind one. The
WebSocket upgrade the client side already parses makes a server-side
WebSocket a later addition (live previews, a chat block). The server module
is UI-free and belongs in UltraNet's archive, absorbed into the shared core
like the rest of the module (`AGENTS.md`, *One shared core*).

Routes the UltraPress server answers:

| Route | Does |
|---|---|
| `GET /…` | the published static files, with the cache headers and the isolation headers when threads are kept |
| `POST /api/form/<id>` | a form block's submission: stored, mailed to the owner, rate-limited, with a proof-of-work or a hidden-field check instead of a third-party captcha |
| `/api/auth/…` | sign-up, sign-in, sign-out, password reset, TOTP, OAuth2 sign-in (§7.6) |
| `/api/shop/…` | cart, checkout session creation, order status; `/api/shop/webhook/<provider>` for the provider's callback (§7.5) |
| `/api/download/<id>` | counts, checks membership or purchase, answers the file or a signed expiring URL (§7.11) |
| `/api/newsletter/…` | subscribe with double opt-in, confirm, unsubscribe (§7.9) |
| `/api/stats` | view counts per page, written from the static-file route; read by the editor for "most popular" and the dashboard |
| `/api/search?q=` | full-text search over the shadow text (SQLite FTS5 if the bundled SQLite is built with it [unverified], else a token table of our own) |

What it does not do: render pages. Pages are files; the server adds the
dynamic answers the renderer fetches through `WASMBrowser::FetchAsync` (and
the UltraWeb app through `uc_fetch`). That keeps the server small, and
makes the static tier a strict subset of the server tier.

**Who can run it:** a VPS, a container, a home server, ULTRA OS. Not a
PHP-only shared host — that is the honest difference from WordPress, and
the reason the static tier exists.

---

## 7. The twelve features, each on the parts above

### 7.1 Importing a WordPress site (not in the brief, but the first thing a user does)

WordPress exports a site as WXR, an RSS-based XML file with every page,
post, menu, category, attachment URL and the author list; post bodies are
HTML (or block-editor HTML with `<!-- wp:… -->` comments). The importer in
`engine/`:

- parses the XML with TinyXML2 behind an UltraPress-owned reader;
- turns each body into blocks: the `wp:` comment markers map directly
  where a block exists (paragraph, heading, image, columns, gallery, list,
  quote), and anything else becomes one rich-text block through
  `ImportHTMLToRichDocument`;
- downloads attachments through UltraNet into `media/`;
- rebuilds menus from the `nav_menu_item` posts, and redirects from old
  permalinks to new slugs.

ultraos.eu is the first site to import; its front page is already in the
tree as a set of blocks in all but name, which makes it the acceptance
test: the imported site, rendered, must look like the demo.

### 7.2 Page manager and menu manager (feature 1)

The site tree in `UltraCanvasTreeView` (pages, posts, products, media),
with pin, status, language, template and SEO fields in a property pane;
drag to reorder; the page list's sorts of §5.1. Menus are named lists of
items edited in a second tree and referenced by the header and footer
blocks.

### 7.3 Page editor with predefined blocks (feature 2), and plugins

The editor is the renderer plus handles: a block palette on the left, the
live page in the middle, properties on the right. Blocks are inserted,
moved and nested; text blocks edit in place with `UltraCanvasRichTextEdit`;
pictures come from the media library (`UltraCanvasAlbum`).

Plugins extend in three sizes:

1. **Block packs** — data only: new block *compositions* of existing
   renderers (a "pricing table" made of columns and cards), with
   properties and defaults. Safe, portable, the common case.
2. **Renderer plugins** — C++ that adds a block type. On the desktop they
   load through `UltraCanvasElementRegistry` as a DSO; in the browser a
   plugin is a second Emscripten module loaded with `dlopen` (Emscripten's
   `MAIN_MODULE` / `SIDE_MODULE` linking) [unverified in this tree: the
   port has not been built as a main module]; in UltraWeb it is a further
   wasi module. Phase 0 checks the side-module build; if it is too costly,
   renderer plugins are compiled into a site-specific renderer build on
   the UltraPress build service (§7.4), not on the user's machine.
3. **Server plugins** — handlers added to `ultrapress-server`, as DSOs
   through the same loader, under a declared route prefix.

Every plugin is a `.ucpkg` (VirtualFS) with a manifest (name, version,
block types, routes, permissions), signed with the shop's key (UltraCrypt),
and listed in the site's `plugins/`. A plugin never sees the site owner's
secrets; the server hands it only the request and the database handle of
its own namespace.

### 7.4 Theme management (feature 3)

A theme is `theme.json`: a palette (named colours with light and dark
variants), a type scale (families, sizes, weights), spacing, radius and
shadow scales, and per block type the style variants it offers and their
defaults; plus the header and footer compositions. The renderer resolves
every block's look from the theme, so switching themes re-renders the same
content — WordPress's promise, kept, because no theme contains markup. A
theme editor in the app changes values and shows the live page; a theme is
shared as a `.ucpkg` through the same shop as plugins. Importing a theme
from an existing site's stylesheet is a reader task (HTMLReader's cascade
gives colours and fonts) for a later phase.

### 7.5 Payment system (feature 5)

```cpp
class IPaymentProvider {              // engine/payments/
public:
    virtual Result CreateCheckout(const Order& order, const std::string& returnUrl,
                                  const std::string& cancelUrl, CheckoutSession& out) = 0;
    virtual Result ParseWebhook(const UltraNetHttpServerRequest& request, PaymentEvent& out) = 0;   // verifies the signature
    virtual Result Refund(const std::string& paymentId, Money amount) = 0;
};
```

Stripe Checkout, PayPal Checkout and Mollie first — all three are hosted
pages the buyer is redirected to, so no card number ever reaches UltraPress
and the isolation headers of §4.3 never meet an iframe [provider details
unverified]. The server creates the session, the renderer redirects, the
webhook marks the order paid, and the order is mailed (§7.8) and exported:
an order is a UltraFIBU document (XRechnung for business buyers, the PDF
invoice the FIBU writer already produces), with the shop's tax keys mapped
onto FIBU's. Keys live in UltraVault on the server under
`ultrapress.<site>.<provider>`; prices use FIBU's money type once it exists
(`UltraFIBUDesignProposal.md` §6.1), never a double. EU VAT, OSS and the
digital-goods rules are FIBU's problem, deliberately: UltraPress records
the sale, FIBU books it.

### 7.6 Member section (feature 9)

Accounts on the server: e-mail and password (Argon2id through
UltraCrypt, which already offers `SetPassword` / `VerifyPassword` with a
constant-time compare; a salted hash, never reversible), optional TOTP with the authenticator's library, sessions as
signed cookies, sign-in with Google or Microsoft through UltraNet's OAuth2
app registry. Members gate pages (`members only`), downloads, prices and
the community. Roles: owner, editor, member, with the editor role used by
the desktop app's publish step, which signs in to the server the same way.
GDPR: export and delete my data as two routes, from the start.

### 7.7 Plugin shop (feature 4) and plugin community (feature 8)

The shop is one more UltraPress site — the server tier with the product,
download and member blocks — run by the project: plugin and theme packages
as products, free or paid, with the payment stack of §7.5 and the download
manager of §7.11 serving signed packages. The desktop app browses it
through the server's API and installs into `plugins/`. The community
section is the same site's forum: a thread-and-reply block set over the
member section, with moderation (owner and editors), notifications by mail
and an RSS feed; nothing federated in the first version. That the shop and
the community are built *with* UltraPress is the point: they are its first
dynamic site, and what they need is what every shop owner needs.

### 7.8 E-mail services (feature 7)

The server sends through the SMTP plugin with UltraMail's outbox pattern
(persistent queue, retries, per-recipient status): order confirmations,
form submissions, sign-up confirmations, password resets, download links.
Credentials for the site owner's SMTP relay sit in UltraVault; a provider
with an HTTP API (Postmark, Mailgun, Brevo) is a second `IMailSender`
behind the same queue [provider APIs unverified]. Bounces come back through
the IMAP plugin into the member and newsletter tables.

### 7.9 Newsletter designer (feature 10)

The page editor with the e-mail block subset, rendered through the HTML
emitter's e-mail mode (table layout, inline styles, the picture rule of
mail clients) and previewed in the app through the HTMLReader — the same
reader UltraMail shows mail with, so the preview is honest. Lists with
double opt-in, a one-click unsubscribe header, sending in batches through
the outbox, opens not tracked by default (a tracking pixel is the thing
`ImportHTMLToRichDocument` strips from incoming mail; the same restraint
applies outgoing), and the issue archived as a page of the site.

### 7.10 SEO tracking (feature 6)

Three layers. The *emitted* layer: the shadow page's metadata, structured
data, sitemap, canonical URLs, redirects from the importer, and an on-save
check in the editor (title length, missing descriptions and alt texts,
broken internal links, duplicate slugs). The *measured* layer: the
server's view counts per page and referrer (no cookies, no fingerprinting,
counted server-side), shown in the app on the Charts elements. The
*external* layer: Google Search Console's and Bing Webmaster's APIs for
impressions, clicks and position per query, through UltraNet's OAuth2
flow, as an optional connector in a later phase [APIs unverified].

### 7.11 Download manager (feature 11)

Files in `media/downloads/` with a record each (title, version, licence,
count, gate: public, members, purchasers of product X). On the static tier
they are plain files; on the server tier the download route counts,
checks the gate and answers the bytes or a signed URL with an expiry, so a
link cannot be shared past its hour. Large files can live on an UltraCloud
provider the server redirects to.

### 7.12 Website builder assistant (feature 0)

A wizard (`UltraCanvasStepper`): what the site is for, the business name,
languages, the pages wanted, a palette and a tone. The answers go to
UltraAI's `ITextLLM` with a structured-output schema that *is* the content
model — a site, pages of blocks, menus, a theme — so the model fills data,
never writes code, and the result opens in the editor as a draft. With the
mock adapter the wizard is tested offline; with a local model it runs
without a cloud. It comes after the content model is stable, because it
produces that model.

---

## 8. Build, versioning and testing

- **Product declaration** per `AGENTS.md` *Versioning*: `Docs/UltraPress/CHANGELOG.md`
  with `#### YYYY-MM-DD *x.y.z*`, one `_ultracanvas_declare_product()` line
  in `cmake/UltraCanvasVersion.cmake`, `ULTRAPRESS_VERSION` in the window
  title, and a `Docs/Legal/Privacy` declaration before the first release.
- **Targets:** `UltraPressEngine` (library), `UltraPress` (app),
  `ultrapress-server` (command-line, linked like UltraFIBU's cli), and the
  renderer built by `wasm-build.yml` — which stops being manual-only: a
  release builds the renderer and publishes `ultrapress-<version>.wasm` as
  a release artefact beside the desktop packages, and the publisher
  downloads it (hash-checked, `cmake/UltraCanvasWasmtime.cmake` style)
  rather than requiring Emscripten on the user's machine. A **build
  service** for site-specific renderers (§7.3, option 2) is the same
  workflow with a plugin list as input, and is optional.
- **Packaging:** `build_app_bundle` in `package-macos.sh`, the Linux and
  Windows scripts; the server as a `build_cli_tool`.
- **Tests, without a display and without a network** (the UltraMail and
  UltraWeb pattern):
  - the content model round-trips through JSON;
  - the block renderer against the recording factory: a page produces the
    expected element kinds and properties; the ultraos.eu demo rebuilt
    from blocks produces the demo's tree;
  - the HTML emitter against fixtures, and its output parsed back by
    `HTML::Parser` to prove it is well-formed;
  - the WXR importer against a fixture export (ultraos.eu's, once
    exported), compared block by block;
  - the server's routes through `UltraNet_HttpServe` on a loopback port,
    against SQLite: forms, sign-in, a fake payment provider's webhook, a
    gated download;
  - the publisher against UltraCloud's in-memory demo provider;
  - the assistant against the UltraAI mock adapter.
  - screenshot tests of rendered pages under Xvfb, the
    `TextMetricsScreenshotTest` way, for the theme work.
- **Checks that apply:** the UI-reuse, callback-cycle, locale-number,
  path-string, Win32-name and HTML-reuse scripts; the HTML *emitter* is
  not a reader and does not fall under the HTML-reuse rule, but every
  `&entity;` it writes goes through one table, and every number through
  `FormatFloatClassic`.

---

## 9. Phases

**Phase 0 — measure, decide (short). Started 2026-10-13.** The baseline is
recorded (§4.3: 11.24 MB raw, 3.9 MB Brotli for the front page demo), and
`wasm-build.yml` now builds the levers as variants beside it — `min-size`
(`-Os`), `no-vips` (`-DULTRACANVAS_ENABLE_VIPS=OFF`; the demo's PNG pictures
then do not load, the module is the measurement), `four-fonts`
(`-DUC_WASM_FONTS=…`) and `single-thread` (`-DULTRACANVAS_WASM_THREADS=OFF`
against a sysroot built with `WASM_THREADS=0`) — each with its own size
table; the window logs its time to first paint to the browser console, so a
tester on a laptop, a phone, Firefox or Safari reads one number. Still to
do: run those tests; whether a single-threaded build links and runs; Firefox and
Safari; whether an Emscripten side module loads. (The History API,
`PushUrl` and `SetOnNavigate`, is already in `UltraCanvasWASMSupport.h`.)
Decide the HTML shadow with the user (§11, question 1). Deliverable: the numbers in this document's §4.3,
and a go / no-go on B.

**Phase 1 — websites, static tier.** The content model, the block
registry and the first fifteen blocks, the block renderer with the desktop
factory, the editor with live preview, the theme model and two themes, the
HTML emitter (shadow, sitemap, feed), the WXR importer, the publisher onto
UltraCloud providers, and the renderer release build. Done when ultraos.eu
is imported, edited, published to a test host and renders in Chromium,
Firefox and Safari as the demo does, with the shadow indexed by a crawler.

**Phase 2 — the server tier.** `UltraNet_HttpServe`; `ultrapress-server`
with static serving, forms, view counts, search, the outbox and SMTP; the
member section with TOTP and OAuth2 sign-in; the download manager with
gates; newsletter lists, double opt-in and the newsletter designer. Done
when the server runs the imported site on a VPS behind Caddy and a
newsletter issue reaches a mailbox.

**Phase 3 — the shop.** Products, cart and checkout blocks; the payment
interface with Stripe, PayPal and Mollie; webhooks; orders; the FIBU
export; customer accounts on the member section. Done when a test purchase
through each provider's sandbox produces a paid order and a FIBU document.

**Phase 4 — plugins, the shop site, the community, UltraWeb.** The plugin
manifest and `.ucpkg` signing; block packs; renderer plugins on the desktop
and in the browser (or the build service); server plugins; the project's
shop-and-community site running on UltraPress; the UltraWeb element
factory and `site.wasm`, once the generated proxies exist. Done when a
third-party block pack installs from the shop and renders, and the
imported site runs natively in UltraWeb.

**Phase 5 — the assistant and polish.** The website-builder wizard on
UltraAI; theme import from stylesheets; Search Console; a hidden DOM
mirror for screen readers; HTTP/2 or WebSocket on the server if a feature
asks for it.

The order puts the static website first because it needs no new framework
module and migrates ultraos.eu; the server second because every later
feature depends on it; the shop third because it is the brief's second
purpose and the riskiest in regulation; plugins fourth because a plugin
system is only worth building for a product that exists.

---

## 10. Risks

| Risk | Mitigation |
|---|---|
| The renderer is too big or too slow for a public website | Measured: 3.9 MB Brotli for the front page demo (§4.3); the no-libvips and font-subset builds are the first levers; the shadow page paints text at once; caching by content hash means one download per release. If the number is unacceptable, the fallback is C-first: UltraWeb-native on ULTRA OS, the shadow on the open web, until the port is leaner. |
| Threads force isolation headers the site cannot set, or block payment and embed widgets | A single-threaded renderer build; hosted checkout by redirect; no cross-origin embeds in v1. |
| A canvas site is invisible to crawlers and assistive technology | The shadow (§4.4); a DOM mirror later. Decided with the user before Phase 1. |
| No HTTP server in the framework; writing one is security-sensitive | A deliberately small HTTP/1.1 server behind a reverse proxy that terminates TLS; body and header limits; fuzzing of the request parser as the HTMLReader is fuzzed; no TLS termination in v1. |
| Shared hosts cannot run the server | The static tier; the documentation says plainly which features need a server. |
| Payments and VAT regulation | Hosted provider pages only; money and tax handled by UltraFIBU's rules, not reimplemented; the provider sandboxes in the test suite. |
| Plugins running arbitrary code in a visitor's browser or on the site's server | Signed packages from the shop; renderer plugins in the browser are sandboxed by the browser; server plugins get a namespaced database and a declared route prefix; in UltraWeb the sandbox is the host's. A permission list in the manifest, shown before install. |
| The editor and the published page drift (WordPress's chronic problem) | They are the same renderer and the same Cairo + Pango pipeline; the screenshot tests pin it. |
| Firefox and Safari differences in the port | Phase 0 lists them; the port's limitations table is the backlog. |
| Scope: twelve features and two products | The phases ship a usable product at every step; Phase 1 alone replaces a WordPress content site. |

---

## 11. Open questions for the user

1. **The HTML shadow.** Is a plain, unstyled, script-free HTML copy of each
   page — for search engines, link previews, mail clients and screen
   readers — acceptable alongside the WebAssembly page, or must the output
   be WebAssembly only, accepting that the site is then unindexed and
   inaccessible? The proposal recommends the shadow (§4.4).
2. **Hosting.** Which hosts do the first users have: classic shared hosts
   (FTP, PHP only), a VPS, or ULTRA OS machines? This decides how much the
   static tier must carry and whether the server tier can assume a reverse
   proxy.
3. **Thread-free renderer.** If the single-threaded build costs a feature
   (Phase 0 will say which), is that feature needed on a website, or may
   the renderer go without it?
4. **ultraos.eu first?** Is a WXR export of the current site available, so
   the importer and the acceptance test can be built against the real
   thing from day one?
5. **Payment providers and countries.** Stripe, PayPal and Mollie are the
   proposal's three; which ones, which currencies, and is SEPA direct
   debit needed? Are digital goods (downloads) the main product?
6. **UltraWeb-native priority.** Should `site.wasm` for UltraWeb come
   before the shop (Phase 2) rather than in Phase 4? It is the showcase
   for both products but it waits on UltraWeb's generated proxies either
   way.
7. **Where the shop and community live.** Run by the project on a
   project server (the proposal), or shipped so that any site owner can
   run their own plugin shop?
8. **Languages.** Is the first user interface German, English, or both,
   and does the content model need per-page languages from the start
   (the proposal says yes: `language` and `hreflang` are in §5.1)?
9. **The name.** "UltraPress" throughout this document; "WorldPress 2.0"
   is the brief's subtitle. The product name goes into the changelog, the
   window title and the renderer's file name, so it is decided once.

---

## 12. Summary

The repository already contains a website rendered by UltraCanvas in a
browser — the ultraos.eu front page — and every module a site builder and
shop need except two: an HTTP server and a theme model. UltraPress turns
that one hand-written page into a product by making the page data, the
renderer a release artefact, and the editor the renderer itself. The
output is WebAssembly in the sense the brief means: no page content,
layout, styling or behaviour is HTML, CSS or JavaScript. One plain HTML
shadow per page is recommended so that search engines, mail clients and
screen readers still see the site. The one number this rests on is measured:
the front page demo is 3.9 MB Brotli-compressed, once per release, and
Phase 0 tries the levers that shrink it; Phase 1 ships a static website builder
that migrates ultraos.eu; Phases 2 and 3 add the server and the shop;
Phase 4 the plugins and the native UltraWeb output.
