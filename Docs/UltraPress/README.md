# UltraPress

**Status:** Phase 1 started. UltraPress 0.1.0 is the headless engine and the
`ultrapress` command line: a site as a folder of JSON files, the block
registry, the HTML shadow of every page, and the static publish. The editor,
the element renderer and the WebAssembly output follow
([`UltraPressDesignProposal.md`](../Research/UltraPressDesignProposal.md)).
**Last Modified:** 2026-10-13
**Author:** UltraCanvas Framework / ULTRA OS

UltraPress is the website and shop builder of ULTRA OS: a WordPress-class
system built on UltraCanvas whose published pages are rendered by the
framework itself, compiled to WebAssembly, with a plain HTML shadow of every
page for search engines, link previews, mail clients and screen readers.

This app versions itself: [`CHANGELOG.md`](CHANGELOG.md).

## The command line

```bash
ultrapress new mysite --name "My site" --url https://my.example   # a starter site
ultrapress check mysite                                           # every problem, exit 1 when there is one
ultrapress page mysite home                                       # the front page's HTML shadow, to stdout
ultrapress publish mysite out                                     # the static tier, into out/
ultrapress publish mysite out --renderer ultrapress-0.1.0.js      # with the renderer referenced (later)
```

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

It links the core library for `UltraCanvasJSON` and the HTMLReader's
`EscapeText`, and opens no window. Tests: `Tests/UltraPress/UltraPressEngineTests.cpp`,
built with `-DULTRACANVAS_BUILD_ULTRAPRESS_TESTS=ON`; they save and reload a
site, validate a broken one, parse the shadow back with `HTML::Parser`, and
publish the example site into a temporary folder.

## Not yet

The editor with the live preview (the block renderer on real elements), the
WebAssembly renderer release and its reference from the shadow, the WordPress
WXR importer, the publisher onto UltraCloud's FTP / SFTP / WebDAV providers,
the newsletter's e-mail mode of the emitter, the Markdown block, and
everything of the server tier (Phase 2): forms and newsletter sign-ups on a
static site need a form service until then.
