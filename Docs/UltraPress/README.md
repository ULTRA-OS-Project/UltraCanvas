# UltraPress

**Status:** Phase 1 under way. UltraPress 0.8.0 is the headless engine, the
`ultrapress` program - the commands and the editor window -, the block
renderer on real elements, the WordPress importer, the publisher onto
web hosts and the media library: a site as a folder of JSON files, the
block registry, the HTML shadow of every page, the static publish, every
block as UltraCanvas elements, blocks added, moved, removed and edited
with the page re-rendered live, a WordPress export read into a site, the
published folder uploaded onto an FTP, SFTP, WebDAV or Nextcloud host
through UltraCloud, only what changed, pictures and downloads brought
into the site's media folder and picked from it, and text edited where it
stands on the page. The WebAssembly output follows
([`UltraPressDesignProposal.md`](../Research/UltraPressDesignProposal.md)).
**Last Modified:** 2026-10-13
**Author:** UltraCanvas Framework / ULTRA OS

UltraPress is the website and shop builder of ULTRA OS: a WordPress-class
system built on UltraCanvas whose published pages are rendered by the
framework itself, compiled to WebAssembly, with a plain HTML shadow of every
page for search engines, link previews, mail clients and screen readers.

This app versions itself: [`CHANGELOG.md`](CHANGELOG.md).

<!-- doc-check: UltraCanvas::UltraCanvasTheme ThemeFor(bool dark); -->

## The window

```bash
ultrapress mysite             # the editor: pages and blocks on the left, the page in the middle, properties on the right
ultrapress mysite --check     # render every page without a window; element counts and warnings
```

`ultrapress` is one program: a command word first (`new`, `check`, `page`,
`publish`, `import`, `hosts`, `help`) runs that command without a window, a
site folder opens the editor. There is no second `UltraPress` binary: two
names that differ only in case are one name on macOS and Windows.

**Publish** writes the static site under `<site>/.ultrapress/publish` and
uploads it onto the site's host on a worker thread; the status line under
the site's name follows file by file. **Host…** opens UltraCloud's
add-account dialog, limited to the web hosts - FTP / FTPS / SFTP, WebDAV,
Nextcloud - and the host added becomes the site's (`hosting` in
`site.json`, with the dialog's upload folder as the site's root on the
host). Without a host of its own the site publishes to the default host;
without any, Publish opens the dialog.

The left column lists the pages and the current page's blocks: select one,
move it with the arrows, remove it, or add one after the selection from
the drop-down of every registered type. The right column is the selected
block's properties, generated from the registry - a text input for a
line, a text area for paragraphs and HTML, a checkbox for a flag, an
integer for a number, and links, items and lists as JSON applied as soon
as it parses - or, with no block selected, the page's title, description,
status and pinning. Every change re-renders the page in the middle;
*Save site* writes the folder (`ui/UltraPressWindow.h`).

The window has two looks. Its colours are a framework theme
(`Apps/UltraPress/ui/UltraPressTheme.h`, `ThemeFor(dark)`: the brand's red accent, its
ink and paper on the framework's light preset; the red lifted on the dark
one), and every element of the columns and the dialogs follows that theme,
so *Dark look* / *Light look* in the side column switches them all at
once; the side column keeps its night blue in both, through the theme's
own `press.*` roles, and the page in the middle keeps the site's colours,
because it is the site. The choice is remembered in
`<settings folder>/UltraPress/look` and read before the window opens.

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
ultrapress hosts                                                  # the hosts a site can be published to
ultrapress hosts add ftp --server sftp://my.example --user me --folder /public_html   # a host; the password from --password or ULTRAPRESS_HOST_PASSWORD
ultrapress hosts use mysite me-at-my.example --path /public_html  # remember it in site.json
ultrapress publish mysite out --to site                           # write, then upload what changed onto the site's host
ultrapress publish mysite out --to "me at sftp://my.example" --path /www --force   # onto a named host, everything
ultrapress publish mysite out --to site --prune                   # and delete on the host what the site no longer has
ultrapress hosts add ftp --server sftp://my.example --user me --key ~/.ssh/id_ed25519   # SFTP with a key (the password: its passphrase)
ultrapress media mysite                                           # every file under media/: kind, size, uses
ultrapress media mysite add ~/Pictures/launch.png                 # copied in as media/launch.png
ultrapress media mysite remove media/launch.png                   # deleted, unless a page uses it
ultrapress media mysite prune                                     # every unused file deleted
```

`import` reads a WordPress export (Tools > Export in WordPress, the WXR
file): the site's name, address, language and owner, every page and post
with its slug, status and date, the front page (the page named home, or
`--front <slug>`), the menus with their nesting, and the bodies as blocks.
The block editor's markers map onto the registry where a block exists -
paragraphs, headings, images, covers, quotes, buttons, columns, galleries,
lists, spacers, HTML - and everything else, and classic content, becomes
rich text; the report says what was kept that way. Attachment URLs are
rewritten to `media/<file>` under the library's clean names and fetched with `--media`
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
  `MakeSlug`; text in a block: `SplitParagraphs`, `TextLocation` /
  `SetBlockText` (a property, an item, an item's key, one paragraph) and
  `ResolveBlockPath`.
- `UltraPressBlocks.h` - the registry and `ValidateSite`.
- `UltraPressHtml.h` - `HtmlEmitter`: `Page`, `Sitemap`, `Robots`, `Feed`,
  `Block`; `HtmlOptions` names the renderer script, the UltraWeb app and
  the site folder, with which every `<img>` carries its width and height.
  Every piece of text goes through `HTML::EscapeText`; `richtext` and `html`
  blocks are the owner's own HTML and pass through.
- `UltraPressPublish.h` - `PublishStatic` (the output mirrors the site:
  what an earlier publish wrote and this one does not is removed, the
  deploy manifest excepted; `removeStale` turns it off) and
  `CreateStarterSite`.
- `UltraPressMedia.h` - the media library: `ListMedia` (kind, size,
  pixels, uses), `MediaReferences`, `ValidateMedia`, `ImportMedia` (a
  clean, unique name under `media/`: `CleanMediaName`, the importer's rule
  too), `RemoveMedia` (never a used file), `PruneMedia`, `ImageDimensions`
  (from the header, no decoding), `MediaKind`, `HumanSize`.
- `UltraPressDeploy.h` - `DeployFolder`: the published folder onto a host
  through an `Uploader` (two functions: make a folder, put a file), the
  root's folders created first, only the files whose `Fingerprint` changed
  since the last upload to that host (`.ultrapress-deploy.json` beside the
  output remembers them per host), stopping after three failures in a row.
  With `prune`, what the manifest knew and the output no longer has is
  deleted, then the folders that left empty, innermost first; never
  anything the deploy did not upload itself. Headless and free of
  UltraCloud; `--force` sends everything.
- `UltraPressImport.h` - `ImportWordPress` / `ImportWordPressFile` with
  `ImportOptions` (the front page, a media fetcher, drafts) and
  `ImportReport`; `BlocksFromWordPressContent` maps the block editor's
  markup. TinyXML2 reads the export, privately.

It links the core library for `UltraCanvasJSON` and the HTMLReader's
`EscapeText`, and opens no window. Tests: `Tests/UltraPress/UltraPressEngineTests.cpp`,
built with `-DULTRACANVAS_BUILD_ULTRAPRESS_TESTS=ON`; they save and reload a
site, validate a broken one, parse the shadow back with `HTML::Parser`,
publish the example site into a temporary folder, and deploy a folder
through a recording uploader and through the hosts onto UltraCloud's
in-memory provider.

### The hosts

`Apps/UltraPress/hosting/UltraPressHosting.h`, target `UltraPressHosting`,
built where UltraCloud is (`-DBUILD_ULTRACLOUD=ON`, the default): `Hosts`
opens the program's host list - UltraCloud accounts in `hosts.db` in the
program's data folder (`UltraCanvasDataFolder("UltraPress")`:
`%APPDATA%\UltraPress`, `~/Library/Application Support/UltraPress`,
`~/.local/share/UltraPress`; `Hosts::DataDirectory`), their passwords in
the credential vault beside it, the way UltraMail keeps its accounts; a
list that 0.5 to 0.7 kept under the settings folder is moved over on the
first open (`AdoptLegacy`) - and gives `DeployFolder` an `Uploader` onto one of them
(`UploaderFor`). The providers are UltraCloud's: `ftp` (an `ftp://`,
`ftps://`, `ftpes://` or `sftp://` server; the upload creates missing
folders itself), `webdav` and `nextcloud` (`https://`; folders through
MKCOL, outermost first). A site names its host and the folder on it in
`site.json`:

```json
"hosting": { "account": "ftp-me-my-example", "path": "/public_html" }
```

`ultrapress hosts use` writes it; the window's Host… button does the same.
An `sftp://` host signs in with a password or a private key (`--key`, or
the dialog's *Private key* row; the password is then the key's passphrase).
The window's Publish prunes; the command line prunes with `--prune`.

**Text in place.** Click a piece of text on the page - a title, a
paragraph, a feature, an answer - and a field takes its place with the text
in it; the block is selected on the left and the right. Enter keeps a
title, Ctrl+Enter a paragraph, Escape drops the change, and so does leaving
a field. One paragraph of a long text is edited on its own; an FAQ's
question on a second click, once it is open. A rich text block has an
*Edit text…* handle that opens the rich text editor in its place, with a
toolbar (bold, italic, underline, link, paragraph style, lists, quote,
undo, redo) above it and Done and Cancel below. An HTML block is edited in
the properties pane.

**Pictures.** A picture property (the hero's and the card's `picture`, the
picture block's `src`) has **Pick…** beside its path. It opens the media
library: every file under `media/` with a thumbnail, its kind, pixels,
size and how many places use it; **Add file…** opens the file dialog and
copies the chosen file into `media/` under a clean name; **Remove** deletes
a file nothing uses, **Remove unused** every such file; **Choose** puts the
path into the property and the preview shows the picture in its own shape. **Media library…** in the side column opens the same
dialog to browse.
Without UltraCloud the program says so, and `ultrapress publish` still
writes the folder to upload by other means.

## Not yet

New pages and menus from the window, undo, the WebAssembly renderer release
and its reference from the shadow, the newsletter's e-mail mode of the
emitter, the Markdown block, and
everything of the server tier (Phase 2): forms and newsletter sign-ups on a
static site need a form service until then.
