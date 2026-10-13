# UltraPress

**Status:** Phase 1 under way. UltraPress 0.4.0 is the headless engine, the
`ultrapress` command line, the block renderer on real elements, the editor
window and the WordPress importer: a site as a folder of JSON files, the
block registry, the HTML shadow of every page, the static publish, every
block as UltraCanvas elements, blocks added, moved, removed and edited
with the page re-rendered live, and a WordPress export read into a site.
The publisher onto web hosts and the WebAssembly output follow
([`UltraPressDesignProposal.md`](../Research/UltraPressDesignProposal.md)).
**Last Modified:** 2026-10-13
**Author:** UltraCanvas Framework / ULTRA OS

UltraPress is the website and shop builder of ULTRA OS: a WordPress-class
system built on UltraCanvas whose published pages are rendered by the
framework itself, compiled to WebAssembly, with a plain HTML shadow of every
page for search engines, link previews, mail clients and screen readers.

This app versions itself: [`CHANGELOG.md`](CHANGELOG.md).

## The window

```bash
UltraPress mysite             # the editor: pages and blocks on the left, the page in the middle, properties on the right
UltraPress mysite --check     # render every page without a window; element counts and warnings
```

The left column lists the pages and the current page's blocks: select one,
move it with the arrows, remove it, or add one after the selection from
the drop-down of every registered type. The right column is the selected
block's properties, generated from the registry - a text input for a
line, a text area for paragraphs and HTML, a checkbox for a flag, an
integer for a number, and links, items and lists as JSON applied as soon
as it parses - or, with no block selected, the page's title, description,
status and pinning. Every change re-renders the page in the middle;
*Save site* writes the folder (`ui/UltraPressWindow.h`).

The page is rendered on real UltraCanvas elements by the block renderer
(`Apps/UltraPress/render/UltraPressRender.h`, target `UltraPressRender`),
built the way the ultraos.eu front page demo is: bands whose centred
column is Block-laid-out so wrapped text measures, wrapping Flex rows for
columns and buttons. The hero and call to action sit on the theme's night
colours, FAQs are an accordion, forms are real inputs whose fields reach
`RenderOptions::onFormSubmit`, the newsletter calls
`onNewsletterSubscribe`, links call `onLink` with a page's path or a URL,
rich text and HTML blocks go through the HTML reader's element builder,
and a picture the site folder lacks is a captioned tile. The theme's
colours (`accent`, `ink`, `inkSoft`, `paper`, `paperSoft`, `rule`,
`night`, `nightSoft`, `onDark`, `tile`) and `fontFamily` reach it through
`PaletteFromTheme`. Under Emscripten the same renderer is the published
page; on the desktop it is the editor's preview, pixel for pixel.

## The command line

```bash
ultrapress new mysite --name "My site" --url https://my.example   # a starter site
ultrapress check mysite                                           # every problem, exit 1 when there is one
ultrapress page mysite home                                       # the front page's HTML shadow, to stdout
ultrapress publish mysite out                                     # the static tier, into out/
ultrapress publish mysite out --renderer ultrapress-0.1.0.js      # with the renderer referenced (later)
ultrapress import export.xml mysite --media                       # a WordPress export as a site, attachments fetched
```

`import` reads a WordPress export (Tools > Export in WordPress, the WXR
file): the site's name, address, language and owner, every page and post
with its slug, status and date, the front page (the page named home, or
`--front <slug>`), the menus with their nesting, and the bodies as blocks.
The block editor's markers map onto the registry where a block exists -
paragraphs, headings, images, covers, quotes, buttons, columns, galleries,
lists, spacers, HTML - and everything else, and classic content, becomes
rich text; the report says what was kept that way. Attachment URLs are
rewritten to `media/<year>/<month>/<file>` and fetched with `--media`
(through UltraNet) or listed. A post's dated WordPress address is reported
as a redirect to its new path. `--no-drafts` leaves drafts out.

`publish` writes `index.html` for the front page and `<slug>/index.html` for
every other published page, `sitemap.xml`, `robots.txt`, `feed.xml` (the
posts), an `.htaccess` with the cache and type rules an Apache host needs,
and copies `media/`. Upload the folder to any web host; the files are all it
takes. Drafts are left out unless `--drafts` is given.

## The site folder

```
mysite/
├── site.json        name, tagline, url, language, languages, owner
├── theme.json       name, colors (name → #rrggbb), fontFamily
├── menus.json       {"main": [...], "footer": [...]}; an item has title and page (a slug) or url, and items
├── pages/<slug>.json
└── media/           pictures and downloads; a block names them by path
```

A page:

```json
{
  "slug": "the-plan", "title": "The development plan", "kind": "page",
  "status": "published", "date": "2026-10-13", "pinned": false,
  "seo": {"description": "…", "image": "media/plan.png", "noindex": false},
  "blocks": [
    {"type": "hero", "props": {"kicker": "…", "title": "…", "text": "…",
                               "buttons": [{"title": "Learn more", "page": "about", "primary": true}]}},
    {"type": "faq", "props": {"items": [{"q": "…", "a": "…"}]}}
  ]
}
```

`slug` `home` is the front page, at `/`; every other page is at `/<slug>/`.
`kind` is `page` or `post` (dated, in the feed, an `<article>`). `status` is
`published`, `draft` or `scheduled`. A link is `{"page": "<slug>"}` or
`{"url": "…"}`. `Apps/UltraPress/examples/ultraos` is a complete small site.

## The blocks

| Type | Properties |
|---|---|
| `heading` | `text`, `level` (2–4) |
| `text` | `heading`, `text` (paragraphs separated by blank lines) |
| `richtext` | `html` (what the editor wrote; passed through) |
| `hero` | `kicker`, `title`, `text`, `buttons`, `picture` |
| `picture` | `src`, `alt`, `caption` |
| `columns` | `heading`; its children are the columns |
| `card` | `title`, `text`, `picture`, `link`, `linkTitle` |
| `features` | `heading`, `items` (strings) |
| `cta` | `title`, `text`, `buttons` |
| `faq` | `heading`, `items` (`q`, `a`); also emitted as `FAQPage` structured data |
| `quote` | `text`, `cite` |
| `buttons` | `items` |
| `form` | `id`, `heading`, `text`, `fields` (`name`, `label`, `type`, `required`), `submit` - posts to `/api/form/<id>` on the server tier |
| `newsletter` | `heading`, `text`, `list` - posts to `/api/newsletter/subscribe` |
| `downloads` | `heading`, `items` (`title`, `file`, `version`, `size`, `description`) |
| `html` | `html` (the owner's own, passed through) |
| `spacer` | `height` |

The registry is `BlockRegistry::BuiltIn()` in
`Apps/UltraPress/engine/UltraPressBlocks.h`; `ValidateSite` checks a site
against it.

## The engine

`Apps/UltraPress/engine/`, target `UltraPressEngine`, `namespace UltraPress`:

- `UltraPressModel.h` - `Site`, `Page`, `Block`, `Menu`, `Theme`; JSON in and
  out; `LoadSite` / `SaveSite`; `PagePath`, `PageUrl`, `LinkHref`;
  `SortedPages` (pinned first; stored, alphabetic or most popular) and
  `MakeSlug`.
- `UltraPressBlocks.h` - the registry and `ValidateSite`.
- `UltraPressHtml.h` - `HtmlEmitter`: `Page`, `Sitemap`, `Robots`, `Feed`,
  `Block`; `HtmlOptions` names the renderer script and the UltraWeb app.
  Every piece of text goes through `HTML::EscapeText`; `richtext` and `html`
  blocks are the owner's own HTML and pass through.
- `UltraPressPublish.h` - `PublishStatic` and `CreateStarterSite`.
- `UltraPressImport.h` - `ImportWordPress` / `ImportWordPressFile` with
  `ImportOptions` (the front page, a media fetcher, drafts) and
  `ImportReport`; `BlocksFromWordPressContent` maps the block editor's
  markup. TinyXML2 reads the export, privately.

It links the core library for `UltraCanvasJSON` and the HTMLReader's
`EscapeText`, and opens no window. Tests: `Tests/UltraPress/UltraPressEngineTests.cpp`,
built with `-DULTRACANVAS_BUILD_ULTRAPRESS_TESTS=ON`; they save and reload a
site, validate a broken one, parse the shadow back with `HTML::Parser`, and
publish the example site into a temporary folder.

## Not yet

Text edited in place on the page, a picture picker and the media library,
new pages and menus from the window, undo, the WebAssembly renderer release
and its reference from the shadow, the publisher onto UltraCloud's FTP / SFTP / WebDAV providers,
the newsletter's e-mail mode of the emitter, the Markdown block, and
everything of the server tier (Phase 2): forms and newsletter sign-ups on a
static site need a form service until then.
