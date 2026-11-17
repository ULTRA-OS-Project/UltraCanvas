# UltraSite

**The HTML + JavaScript output of a website.** A list of routes goes in;
out comes a folder any static host serves: complete, styled pages, one
content-hashed stylesheet, small content-hashed script modules, the
sitemap, the 404 page and the host's cache and security rules. The build
fails when a link or an anchor inside the site is broken, or when a page
or an asset is over its size budget.

It is built the way [Docusaurus](https://docusaurus.io) builds a site:
every page is already final HTML when it reaches the browser, and the
shared assets are cached for good. It leaves out what makes Docusaurus
heavy: React, hydration and a bundler. The scripts are plain ES modules
that only add behaviour to HTML that already works, so a site written by
UltraSite reads and navigates with JavaScript turned off.

UltraPress and the UltraStore web store each have two outputs:
(a) WebAssembly, drawn by UltraCanvas, and (b) HTML + JavaScript.
UltraSite is (b). The design and the plan are in
[`Docs/Research/UltraSiteStaticSitesProposal.md`](../../Research/UltraSiteStaticSitesProposal.md).

> **Status (2026-11, v0.3, Phases A to C of the proposal):**
> - **Phase A:** routes; the `page` and `post` layouts, plus layouts an
>   app adds; design tokens with a light and an optional dark look; hashed
>   assets; `core.js`; behaviour modules an app registers; the link and
>   anchor check; budgets; the stale-file sweep; `.htaccess`, `_headers`
>   and a Content-Security-Policy.
> - **Phase B:** documentation sites. A Markdown folder becomes doc pages
>   (`AddDocs`, with front matter, categories and translations). The `doc`
>   layout has a sidebar, a table of contents, breadcrumbs and
>   previous/next. Code is highlighted when the site is built. There are
>   admonitions and tabs, a search index per language with a search page,
>   the `search`, `toc`, `tabs` and `copy` modules, a language switcher,
>   and the frame's words in `Labels`.
> - **Phase C:** a site under a path, and a frame (navbar, footer, words)
>   per language.
> - **Users:** UltraPress publishes through it
>   (`Apps/UltraPress/engine/UltraPressSite.h`), including a site's
>   `docs/` folder and `ultrapress docs`. The UltraStore admin backend
>   publishes the web store through it
>   (`Apps/UltraStoreAdmin/core/WebStoreHtml.h`).
> - **Next:** responsive picture variants, precompressed files, a preview
>   server and versioned docs (Phase D).
>
> Sources are under `UltraSite/{include,core,resources}`. The target is
> `UltraSite`, the header `<UltraSite/UltraSite.h>`, and the namespace
> `UltraSite`.

---

## What a build writes

```
out/
  index.html, about/index.html, ...  every route, complete and styled
  404.html                           the app's route "/404.html", or a default one
  assets/site.<hash>.css             the base stylesheet, the tokens, the app's extraCss
  assets/js/core.<hash>.js           on every page: prefetch on intent, the theme toggle
  assets/js/<behaviour>.<hash>.js    only where a page uses it
  assets/<name>.<hash>.<ext>         what Builder::Asset was given
  <prefix>/...                       folders given to AddDirectory, as they are
  sitemap.xml, robots.txt            the indexable routes, with their dates
  .htaccess, _headers                cache rules, the 404 page, the security policy
```

`<hash>` is the first ten hex digits of the file's SHA-256. A changed file
gets a new name, the pages point at the new name, and the old file is
deleted by the next build. That is why the hashed files can be cached for
a year (`Cache-Control: public, max-age=31536000, immutable`) while pages
are checked again on every visit (`no-cache`).

### Every page

The `<head>` contains, in this order:

- the charset, the viewport and the Content-Security-Policy;
- the title and the description;
- the generator, `noindex` where asked, and the colour schemes the page
  supports;
- the canonical URL and the `hreflang` alternates;
- the Open Graph and Twitter card tags;
- the theme script, if the site has a dark look;
- the stylesheet, the core module and the page's behaviour modules;
- the speculation rules;
- the route's `extraHead`;
- the JSON-LD.

The `<body>` is a skip link, the navbar (the brand, the links with the
current page marked `aria-current`, and the theme toggle), then
`<main id="main">` with the layout's output, then the footer.

The theme script is under 200 bytes. It applies a look the visitor chose
before the first paint, so the page never flashes the wrong colours.
Without a stored choice the CSS follows `prefers-color-scheme`. The toggle
is `hidden` until `core.js` shows it, since without script it could not
work.

### The security policy

`script-src 'self'` plus the hashes of the two inline scripts, the theme
script and the speculation rules. No script from another origin and no
inline event handler runs. The policy is in each page as a `<meta>`, and in
`.htaccess` and `_headers` with `frame-ancestors 'self'` added, which a
`<meta>` cannot carry. Pictures may come from `https:`, and inline style
attributes are allowed, because an owner's rich text carries both. An app
adds sources per directive through `SiteConfig::cspAdditions`. A build
without SHA-256 (UltraCrypt without libsodium) writes no policy and warns.

### Prefetch and prerender

- **Chromium:** prerenders a page of the site while the pointer rests on
  its link, through the page's `<script type="speculationrules">`.
- **Other browsers:** `core.js` adds `<link rel="prefetch">` for the page
  on pointer-over, touch or keyboard focus.

Both leave alone:

- the assets, and every prefix in `SiteConfig::noPrefetch` (UltraPress
  passes `/media/` and `/api/`);
- links with `download`, `data-no-prefetch` or `rel="nofollow"`;
- anything that is not a page, meaning a path that ends neither in `/`
  nor in `.html`;
- a visitor who asked to save data.

---

## Using it

```cpp
#include "UltraSite/UltraSite.h"

UltraSite::SiteConfig config;
config.url = "https://example.org";
config.name = "Example";
config.tokens.light = {{"accent", "#2563eb"}, {"bg", "#ffffff"}};
config.tokens.dark = {{"accent", "#60a5fa"}, {"bg", "#0f172a"}, {"fg", "#e2e8f0"}};
config.navbar = {{"Home", "/", {}}, {"About", "/about/", {}}};
config.extraCss = ".hero{padding:4rem 0}";

UltraSite::Builder builder(config);
builder.AddDirectory("site/media", "media");          // copied as it is, links into it checked
builder.AddFile("feed.xml", feedXml);
builder.AddScript("tabs", tabsModuleSource);          // used by data-us="tabs"

UltraSite::Route home;
home.path = "/";
home.title = "Example";
home.description = "What Example is.";
home.bodyHtml = "<section class=\"hero\"><h1>Welcome</h1></section>";
builder.AddRoute(home);

const UltraSite::BuildResult result = builder.Build("out");
if (!result.ok) for (const std::string& error : result.errors) std::fprintf(stderr, "%s\n", error.c_str());
std::fputs(result.SizeReport().c_str(), stdout);
```

<!-- doc-check: std::string feedXml; std::string tabsModuleSource; -->

| Call | What it does |
|---|---|
| `Builder(SiteConfig)` | The site: URL, name, language, tokens, navbar, footer, `extraCss`, budgets, `labels`, `search`, `languageNames` |
| `AddRoute(Route)` | One page: `path` (`/`, `/a/b/`, `/x.html`), `layout` (`page`, `post`, `doc`), `title`, `documentTitle`, `description`, `image` (+ size), `bodyHtml`, `date`, `kind` (`og:type`), `jsonLd`, `alternates` (language → path), `extraHead`, `noindex`, `searchable`; for `doc`: `sidebar`, `toc`, `editUrl` |
| `AddLayout(name, fn)` | A layout of the app's own: `fn(route)` returns what goes into `<main>` |
| `AddScript(behaviour, source)` | A behaviour module (below) |
| `Asset(path)`, `AssetFromText(name, text)` | A file under its hash in `assets/`; returns its URL |
| `AddDirectory(source, prefix)` | A folder copied under `prefix`, not hashed (a media library) |
| `AddFile(path, text)` | A file written as it is (`feed.xml`) |
| `AddSidebar(id, items)` | A sidebar the `doc` layout shows on the routes that name it (`Route::sidebar`) |
| `AddDocs(DocsOptions)` | A Markdown folder as doc pages with their sidebar (below) |
| `Build(out, BuildOptions)` | Writes the folder; `removeStale`, `keep` (files a sweep leaves, such as a deploy manifest), `checkLinks`, `brokenLinksWarn`, `enforceBudgets` |
| `RenderPage(route)` | One page as Build would write it, for previews and tests |

The helpers `RouteFile`, `ContentHash`, `CompressedSize`, `TokensCss`,
`MinifyCss` and `StripScript` are public, for apps that write files of
their own beside the site.

### A site under a path

A site whose `url` has a path - `https://store.example/v1` - is served
from that path, as Docusaurus's `baseUrl` is:

- **Routes.** They keep their site paths (`/app/x/`), and the files go at
  the folder's root (`app/x/index.html`).
- **Addresses.** Every address the builder writes carries the path:
  assets, the navbar, the sidebar, the search, the 404 page, `.htaccess`
  and `_headers`. A body the app writes uses `Builder::Url("/app/x/")`.
- **The link check** reads addresses back in that path, and reports one
  that leaves it.
- `BasePath()` says which path it is.

With `BuildOptions::removeStale` off, a file the folder already holds is
a target links may name. That is how the store's pages link to the
catalog's media in the same folder.

### A frame per language

`SiteConfig::frames` holds another language's navbar, footer, footer line
and `Labels` (`Frame`), so a German page leads into the German pages in
German. A route takes its language's frame; the site's own language, and
any language without a frame, take `navbar`, `footer`, `footerText` and
`labels`.

### Tokens

`Tokens::light` and `Tokens::dark` map names to values, written as
`--us-<name>`. The base stylesheet reads these names:

| Group | Names |
|---|---|
| Colours | `bg`, `fg`, `muted`, `accent`, `accent-fg`, `surface`, `border` |
| Fonts | `font`, `font-heading`, `font-mono` |
| Lengths | `radius`, `width` |

An app's `extraCss` reads any further names it defines. UltraPress adds
`night`, `night-soft`, `on-dark`, `tile` and `accent-hover`.

- **Allowed:** a name is `[a-z0-9-]`. A value with `; { } < >` or a line
  break is refused, so a theme file cannot inject rules.
- **No dark set:** the site is light only. It then has no toggle and no
  theme script, and asks the browser for a light scheme.

### Behaviour modules

A behaviour is an ES module that enhances the elements marked with its
name. The page's HTML must already work without it.

```html
<div data-us="tabs"> ... </div>       <!-- several: data-us="tabs lightbox" -->
```

```js
// tabs: every [data-us~="tabs"] element gets its tab list.
for (const element of document.querySelectorAll('[data-us~="tabs"]')) {
  element.dataset.ready = '1';
}
```

The rules for a module:

- **Loading.** The builder finds the `data-us` names in each page through
  the HTMLReader and links exactly those modules, as
  `<script type="module">`: deferred, and run once the page is parsed.
- **Written once.** A module no page uses is not written.
- **Unknown names.** A name no module provides is an error.
- **Comments.** A `//` comment stands on a line of its own. The build
  removes whole comment lines and indentation, and Brotli or gzip does the
  rest. It does not minify further, since a real minifier would need Node.
- **What it may do.** Fetch nothing from another origin, never change the
  page's text, track nobody, and use no `eval` and no inline handler. The
  security policy enforces the last one.

### Budgets

The default budgets, all in compressed bytes (gzip -9, which is larger
than Brotli, so passing here means passing on a Brotli host):

| What | Budget |
|---|---|
| A page's HTML | 30 KB |
| The stylesheet | 20 KB |
| `core.js` | 4 KB |
| One behaviour module | 3 KB |
| All script one page loads | 15 KB |
| The two inline scripts together (raw) | 1 KB |
| One language's search index | 150 KB |

A build without zlib measures raw sizes, which is stricter. Over budget is
an error, or a warning with `enforceBudgets = false`. `SizeReport()` prints
every measured file with its budget.

### Links

The build checks every `href` and `src` that stays on the site: pages,
assets, files written with `AddFile`, and files under an `AddDirectory`
folder. It also checks that an anchor (`/about/#team`, `#top`) names an
`id` on the target page. Relative links are resolved against the page's
folder, and `/about` finds `/about/`. Links to another origin, `mailto:`,
`tel:` and `data:` are not checked.

---

## Documentation sites

`Builder::AddDocs(DocsOptions)` reads a folder of Markdown the way
Docusaurus reads its `docs/`:

| In the folder | On the site |
|---|---|
| `guide/install.md` | the page `routePrefix` + `guide/install/` |
| `guide/index.md` or `guide/README.md` | the folder's own page, `guide/` |
| a folder without one | a generated page listing what it holds, as cards |
| front matter (`---` lines): `title`, `description`, `slug`, `sidebar_label`, `sidebar_position`, `draft`, `last_update` | the page's title (else its first `# heading`, else its file name), description (else its first paragraph), address, sidebar entry and order, whether it is published, its "Last updated" |
| `guide/_category_.json`: `{"label": ..., "position": ...}` | the folder's sidebar entry |
| `[text](other.md#part)` | the other page's address, the anchor kept |
| `[text](files/notes.txt)` inside the folder | the file as a hashed asset |
| `[text](../../src/main.cpp)` outside it | `sourceUrl` + its path under `sourceRoot` (a repository's file view) |
| a link that leads nowhere | a warning when the docs load, and a broken link at the build |
| `DocsOptions::translations` (`language`, `folder`, `routePrefix`) | the same files in another language, joined page by page (`hreflang`, the language switcher) |
| `exclude` | file and folder names left out wherever they are (`CHANGELOG.md`) |

The sidebar follows the folders, ordered by `sidebar_position` and then by
name. On a page, only the categories that lead to it are expanded; the
others are links to their own pages, as collapsed categories are in
Docusaurus. That way the sidebar adds a few kilobytes to a page however
large the site grows.

### Markdown

`UltraSiteMarkdown.h` turns one page's Markdown into its HTML:

```cpp
#include "UltraSite/UltraSiteMarkdown.h"

UltraSite::MarkdownOptions options;
options.link = [](const std::string& target) { return "/docs/" + target; };   // other.md -> ...
options.copyLabel = "Copy";
const UltraSite::MarkdownResult page = UltraSite::MarkdownToHtml(markdown, options);
// page.html, page.toc (the h2 and h3 with their ids), page.title (the first h1)
```

<!-- doc-check: std::string markdown; -->

- **Reading and writing.** The framework reads the Markdown and writes it
  (`UCRichDocument::FromMarkdown` and `ToHTML`, through the latter's
  `headingId` and `codeBlock` hooks). That reader takes a line as a
  paragraph, as an editor does. A site's Markdown wraps its paragraphs,
  so the lines of a paragraph, a list item and a quote are joined first,
  as CommonMark joins them.
- **Heading ids.** Every heading gets an id spelled as GitHub spells it
  (`Slugify`): lower case, punctuation and dashes dropped, each space a
  `-`, and a repeated heading `-1`, `-2`. So the `other.md#some-heading`
  links a repository already has keep working.
- **Code.** Code is highlighted when the site is built
  (`HighlightCode`, on `UltraCanvasSyntaxTokenizer`: `<span
  class="tok-k">` for a keyword, and so on). The browser runs no
  highlighter.
- **Admonitions.** `:::note Title` … `:::`, and `tip`, `info`, `warning`
  and `danger`. GitHub's `> [!NOTE]` alerts work too.
- **Tabs.** `:::tabs`, then one `::tab[Label]` line per tab, then `:::`.
  Each tab is a `<section>` under its label, readable without script.

### Search

`SiteConfig::search` puts a search box in the navbar. The build writes:

- one index per language, `search/<language>.<hash>.json`, holding each
  page's title, address and description and its sections' anchors,
  headings and text;
- a `/search/` page. Without script it lists every page; with script it
  shows the results for `?q=`.

The `search` module fetches the index the first time the box takes the
focus. It ranks titles above headings above text, lists at most three
hits per page, and is worked with the arrow keys, Enter and Escape. `/`
moves the cursor into the box.

An index over its budget (150 KB compressed) is written again without the
section texts: titles, headings and descriptions only. The build warns
when it does. This repository's own `Docs/` (418 pages, 7 156 sections) is
such a site: 906 KB with the texts, 125 KB without.

### The built-in behaviours

| Module | On | Without it | With it |
|---|---|---|---|
| `search` | every page of a site with search | the box submits to `/search/`, which lists every page | results as you type, keyboard-driven |
| `toc` | doc pages with a table of contents | plain links | marks the heading being read |
| `tabs` | `:::tabs` | the tabs stacked under their labels | a tab list; the choice remembered for every box with the same labels |
| `copy` | code blocks (`MarkdownOptions::copyLabel`) | selectable text | a copy button |

An app's `AddScript` with one of these names replaces it.

### The doc layout

On a wide screen the layout has three columns: the sidebar (sticky), the
page, and the table of contents (sticky). Below 75 rem the table of
contents moves to an *On this page* `<details>` above the text. Below
62 rem the sidebar becomes a panel that the *Menu* link opens with
`#us-sidebar:target`, so no script is needed. The page has breadcrumbs
from the sidebar, then its body, then *Edit this page* (`Route::editUrl`)
and *Last updated* (`Route::date`), then previous and next in the
sidebar's order. A page without a `<h1>` gets its title as one.

### Labels

Every word the frame says is in `SiteConfig::labels` (`Labels`): the skip
link, the navigation landmarks, the theme toggle, the 404 page, the search
box and page, the table of contents, the sidebar's *Menu* and *Close*, the
breadcrumbs, *Previous* and *Next*, *Edit this page*, *Last updated*,
*Copy* and *Copied*, and the language switcher. A site in another language
sets them in it. `SiteConfig::languageNames` names the switcher's
languages (`{"de", "Deutsch"}`).

---

## Tests

`Tests/UltraSite` (ctest `UltraSite`, `-DULTRACANVAS_BUILD_ULTRASITE_TESTS=ON`)
builds a fixture site with two languages, a post, a behaviour, a media
folder and a dark look, and reads it back:

- the head, the frame and the layouts;
- the hashed names, and that they change only with the content;
- a module on exactly the pages that use it;
- the policy in both the page and the host's headers;
- JSON-LD that cannot close its `<script>`;
- the sitemap and the 404 page;
- broken links and anchors, unknown behaviours and budgets as errors;
- the stale-file sweep;
- Markdown: front matter, ids (GitHub's spelling, letters beyond ASCII,
  repeats), wrapped lines, highlighting, admonitions and alerts, tabs, and
  links through the caller;
- `AddDocs`: pages from files and folders, drafts and excluded files left
  out, generated index pages, `_category_.json`, `sidebar_position` and
  `sidebar_label`, links to pages, assets and source, a translation;
- the doc layout's sidebar, breadcrumbs, previous and next, table of
  contents and edit link;
- the search index and page, an index over budget trimmed, and a broken
  link as an error or, with `brokenLinksWarn`, a warning.

UltraPress's engine tests publish its example site through it, `docs/`
included.
