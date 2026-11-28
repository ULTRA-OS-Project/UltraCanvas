# UltraPress

**Status:** Phase 1 under way. UltraPress 0.8 is the headless engine, the
`UltraPress` program - the commands and the editor window -, the block
renderer on real elements, the WordPress importer, the publisher onto
web hosts and the media library: a site as a folder of JSON files, the
block registry, the HTML shadow of every page, the static publish, every
block as UltraCanvas elements, blocks added, moved, removed and edited
with the page re-rendered live, a WordPress export read into a site, the
published folder uploaded onto an FTP, SFTP, WebDAV or Nextcloud host
through UltraCloud, only what changed, pictures and downloads brought
into the site's media folder and picked from it, and text edited where it
stands on the page. A site publishes in one of two outputs: HTML +
JavaScript, through UltraSite (styled pages, the default), or WebAssembly,
whose renderer release follows
([`UltraPressDesignProposal.md`](../Research/UltraPressDesignProposal.md),
[`UltraSiteStaticSitesProposal.md`](../Research/UltraSiteStaticSitesProposal.md)).
**Last Modified:** 2026-11-17
**Author:** UltraCanvas Framework / ULTRA OS

UltraPress is the website and shop builder of ULTRA OS: a WordPress-class
system built on UltraCanvas. A site is published in one of two outputs,
chosen per site: **HTML + JavaScript** - every page complete and styled,
one hashed stylesheet, a few kilobytes of script, the way Docusaurus
publishes a site - or **WebAssembly** - the pages rendered by the framework
itself, compiled to WebAssembly, with a plain HTML shadow of every page for
search engines, link previews, mail clients and screen readers. See *The
two outputs* below.

This app versions itself: [`CHANGELOG.md`](CHANGELOG.md).

<!-- doc-check: UltraCanvas::UltraCanvasTheme ThemeFor(bool dark); -->

## The window

```bash
UltraPress                    # the start window: open a site folder, or create a new site in one
UltraPress mysite             # the editor: pages and blocks on the left, the page in the middle, properties on the right
UltraPress mysite --check     # render every page without a window; element counts and warnings
```

`UltraPress` is one program: a command word first (`new`, `check`, `page`,
`publish`, `import`, `hosts`, `help`) runs that command without a window, a
site folder opens the editor. There is no second `UltraPress` binary: two
names that differ only in case are one name on macOS and Windows. Started
with no folder - a double-click - or with a folder that holds no site, it
opens the start window (`Apps/UltraPress/ui/UltraPressStart.h`): it says
why, *Open a site folder…* goes through the folder dialog onto an existing
site, and a name with *Create it in a folder…* writes the starter site into
a folder of your choice; the editor opens from there. The sites opened last
are offered first, as buttons under *Opened last* (the eight newest, the
whole path as the button's tooltip). The window is the framework's start
page (`UltraCanvasStartPage`) with UltraPress's words and its one field. On Windows the installer
puts *UltraPress* into the Start menu, which starts it this way.

**Publish** writes the static site under `<site>/.ultrapress/publish` and
uploads it onto the site's host on a worker thread; the status line under
the site's name follows file by file. **Host…** opens UltraCloud's
add-account dialog, limited to the web hosts - FTP / FTPS / SFTP, WebDAV,
Nextcloud - and the host added becomes the site's (`hosting` in
`site.json`, with the dialog's upload folder as the site's root on the
host). An FTP / SFTP host is asked for its protocol, its name or address
and its port (empty: the protocol's own), and its private key is chosen
with a file dialog. The sign-in is reported on the console as it happens -
where, as whom, the session's steps, how it ended and the transport's
diagnostics - the way `UltraPress hosts add` reports one, so a host that
refused can be read step by step. Without a host of its own the site
publishes to the default host; without any, Publish opens the dialog.

The left column lists the pages and the current page's blocks: select one,
move it with the arrows, remove it, or add one after the selection from
the drop-down of every registered type. The right column is the selected
block's properties, generated from the registry - a text input for a
line, a text area for paragraphs and HTML, a checkbox for a flag, an
integer for a number, and links, items and lists as JSON applied as soon
as it parses - or, with no block selected, the page's title, description,
status and pinning. *Site settings…* in the side column puts the site
itself there instead: name, tagline, address, language and owner; the
address is checked as typed (`IsSiteAddress`: `https://` and a host, no
trailing slash) and the rule stands under the field, in the error colour,
until it is met. Every change re-renders the page in the middle; *Save
site* writes the folder (`ui/UltraPressWindow.h`).

The window has two looks. Its colours are a framework theme
(`Apps/UltraPress/ui/UltraPressTheme.h`, `ThemeFor(dark)`: the brand's red accent, its
ink and paper on the framework's light preset; the red lifted on the dark
one), set under the application's `Own` theme policy with `ThemeFor` as
the provider (`ui/main.cpp`): until a look is chosen the editor follows
the desktop - a dark desktop, or *Dark* on UOS-Settings' Look page, gives
the dark look, and a change while the editor runs is taken at once - and
the fonts are the desktop's. Every element of the columns and the dialogs
follows that theme, so *Dark look* / *Light look* in the side column
switches them all at once and makes the look the editor's own from then
on; the side column keeps its night blue in both, through the theme's
own `press.*` roles, and the page in the middle keeps the site's colours,
because it is the site. The choice (`look`: `system`, `light` or `dark`)
is remembered with the program's other settings - the sites opened last,
the editor window's size - in
`<settings folder>/UltraPress/settings.json` (`ui/UltraPressSettings.h`)
and read before the window opens.

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

## The two outputs

`"output"` in `site.json` - or *Publish as* in *Site settings…*, or
`--output` for one `publish` run - says what *Publish* writes:

| | `html` (the default) | `wasm` |
|---|---|---|
| What a visitor gets | Every page as complete, styled HTML; the 4 KB core script (prefetch, the theme toggle) | The HTML shadow of every page, which the WebAssembly renderer replaces |
| Written by | UltraSite (`UltraSite/`, `Docs/Modules/UltraSite/README.md`) through `UltraPressSite.h` | `PublishStatic` (`UltraPressPublish.h`) |
| Looks like the editor | Close: the same theme colours as CSS | Exactly: the same elements draw both |
| First visit | The page itself: the example site's front page is 2.3 KB, its stylesheet 3.0 KB and its script 0.8 KB, compressed | The renderer and the page |
| Without JavaScript | The whole site | The shadow |

The HTML + JavaScript output writes the blocks as the shadow's writer does
(`HtmlEmitter` with `HtmlOptions::styled`: the hero's picture fetched first,
every other one lazily, the FAQ an accordion), styled by the blocks'
stylesheet (`BlockStylesheet()`) on the theme's colours as UltraSite tokens
(`ThemeTokens`: `paper`, `ink`, `inkSoft`, `accent`, `paperSoft`, `rule`,
`night`, `nightSoft`, `onDark`, `tile` over the element renderer's
defaults). `"dark"` in `theme.json` is the same names for the dark look:
with it the site follows the visitor's system and offers a toggle that
remembers the choice; without it the site is light only. The font is the
theme's family where the visitor has it, else the system's: nothing is
downloaded. The main and footer menus become the navbar and the footer.
Beside the pages: the hashed stylesheet and script, `feed.xml`,
`sitemap.xml`, `robots.txt`, `404.html`, `.htaccess`, `_headers` and the
media folder. A link to a page or a picture the site does not have, an
anchor that is not there, or a page over its size budget fails the publish
and names the page; `--sizes` lists what every page and asset weighs.

A block type says which outputs show it (`BlockType::htmlOutput`,
`wasmOutput`); every built-in block has both, and `check` reports a block
the site's output cannot show.

### Documentation

A site's `docs/` folder holds Markdown, which the HTML + JavaScript output
publishes at `/docs/` the way Docusaurus publishes its docs:

- **Pages.** A page per file. A folder's `index.md` is the folder's page,
  and a folder without one gets a generated page listing what it holds.
  The sidebar follows the folders (`_category_.json`, `sidebar_position`
  in a file's front matter).
- **The page.** A table of contents, breadcrumbs, previous and next, code
  highlighted when the site is built, and `:::note` / `:::tip` /
  `:::warning` admonitions. `:::tabs` with one `::tab[Label]` line per tab
  gives tabs.
- **Search.** A site with docs gets a search box and a search page.
- **Translations.** `i18n/<language>/docs` holds a translation for every
  language in `site.json`'s `languages`, published at `/<language>/docs/`
  with a language switcher.

Link to it from a menu with `{"title": "Docs", "url": "/docs/"}`, as the
example site does. The WebAssembly output leaves the docs out. The
reference is `Docs/Modules/UltraSite/README.md`, *Documentation sites*.

**Earlier versions** of the docs go in `versioned_docs/<label>/`, ordered
by `versions.json` (`["2.0", "1.0"]`, newest first), else newest first by
name. Each is published at `/docs/<label>/` with a version switcher, and a
banner leads to the latest. **A blog** comes with any site that has posts:

- `/blog/` lists them newest first, with their dates, descriptions and
  tags;
- `/blog/tags/<tag>/` lists each tag's posts;
- a post links its tags, which are `"tags"` in its page file.

**Pictures** of the site's media are `<picture>`s:

- the original where `media/` puts it;
- AVIF and WebP variants, at the widths each role is shown at: the hero's
  picture fetched first, a card's and a picture block's lazily;
- WebP alone where the machine's image library writes no AVIF.

`UltraPress docs` publishes a folder of Markdown on its own, with no
UltraPress site around it. A link out of the folder goes to
`--source-url` plus its path under `--source-root`, which is a
repository's file view. This repository's own `Docs/` builds this way:

```bash
UltraPress docs Docs out --name "UltraCanvas docs" --url https://docs.example.org \
    --source-root . --source-url https://github.com/<owner>/<repo>/blob/main/ \
    --edit-url https://github.com/<owner>/<repo>/edit/main/Docs/ \
    --exclude CHANGELOG.md --exclude changelog.d --sizes
```

## The command line

```bash
UltraPress new mysite --name "My site" --url https://my.example   # a starter site
UltraPress check mysite                                           # every problem, exit 1 when there is one
UltraPress page mysite home                                       # the front page's HTML shadow, to stdout
UltraPress publish mysite out                                     # the site's output, into out/
UltraPress publish mysite out --sizes                             # and what every page and asset weighs
UltraPress docs Docs out --name "My docs" --url https://docs.my.example   # a Markdown folder as a documentation site
UltraPress serve mysite --port 3000                               # look at it on this computer: built, served on 127.0.0.1, rebuilt on a change
UltraPress publish mysite out --precompress                       # with a .gz (and .br) copy beside every page, stylesheet and script
UltraPress publish mysite out --output wasm --renderer ultrapress-0.1.0.js   # the WebAssembly output, the renderer referenced (later)
UltraPress import export.xml mysite --media                       # a WordPress export as a site, attachments fetched
UltraPress hosts                                                  # the hosts a site can be published to
UltraPress hosts add ftp --server sftp://my.example --user me --folder /public_html   # a host; the password from --password or ULTRAPRESS_HOST_PASSWORD
UltraPress hosts add ftp --server sftp://my.example --port 2222 --user me       # on a port that is not the protocol's own
UltraPress hosts use mysite me-at-my.example --path /public_html  # remember it in site.json
UltraPress publish mysite out --to site                           # write, then upload what changed onto the site's host
UltraPress publish mysite out --to "me at sftp://my.example" --path /www --force   # onto a named host, everything
UltraPress publish mysite out --to site --prune                   # and delete on the host what the site no longer has
UltraPress hosts add ftp --server sftp://my.example --user me --key ~/.ssh/id_ed25519   # SFTP with a key (the password: its passphrase)
UltraPress media mysite                                           # every file under media/: kind, size, uses
UltraPress media mysite add ~/Pictures/launch.png                 # copied in as media/launch.png
UltraPress media mysite remove media/launch.png                   # deleted, unless a page uses it
UltraPress media mysite prune                                     # every unused file deleted
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
├── site.json        name, tagline, url, language, languages, owner, output ("html" or "wasm")
├── theme.json       name, colors (name → #rrggbb), dark (the same names, the dark look), fontFamily
├── menus.json       {"main": [...], "footer": [...]}; an item has title and page (a slug) or url, and items
├── pages/<slug>.json
├── media/           pictures and downloads; a block names them by path
├── docs/            Markdown documentation (the HTML + JavaScript output), _category_.json per folder
└── i18n/<lang>/docs the same documentation translated
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
- `UltraPressBlocks.h` - the registry (with each type's outputs) and
  `ValidateSite`.
- `UltraPressHtml.h` - `HtmlEmitter`: `Page`, `Sitemap`, `Robots`, `Feed`,
  `Block`; `HtmlOptions` names the renderer script, the UltraWeb app and
  the site folder, with which every `<img>` carries its width and height.
  Every piece of text goes through `HTML::EscapeText`; `richtext` and `html`
  blocks are the owner's own HTML and pass through.
- `UltraPressSite.h` - the HTML + JavaScript output: `PublishHtml` through
  UltraSite (with the site's `docs/`), `Publish` (the site's output, or
  `PublishOptions::output`), `PublishDocsFolder` (`UltraPress docs`),
  `ThemeTokens`, `BlockStylesheet`, `HasHtmlOutput`. `HtmlEmitter::Parts`
  gives it each page's body, title, description, social picture and
  structured data.
- `UltraPressPublish.h` - `PublishStatic`, the WebAssembly output (the output mirrors the site:
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
`EscapeText`, and UltraSite for the HTML + JavaScript output, and opens no
window. Tests: `Tests/UltraPress/UltraPressEngineTests.cpp`,
built with `-DULTRACANVAS_BUILD_ULTRAPRESS_TESTS=ON`; they save and reload a
site, validate a broken one, parse the shadow back with `HTML::Parser`,
publish the example site into a temporary folder in both outputs, and deploy a folder
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

`UltraPress hosts use` writes it; the window's Host… button does the same,
from the protocol, server and port it asks for (`FtpServerUrl`); on the
command line the port is `--port`, or part of the URL (`sftp://my.example:2222`),
the two storing the same host. A server URL that is not FTP - `https://`,
from the browser's bar - is refused before anything is sent, by `hosts add`
and by the provider. An `sftp://` host signs in with a
password or a private key (`--key`, or the dialog's *Private key* row and
its *Choose* button; the password is then the key's passphrase).
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
Without UltraCloud the program says so, and `UltraPress publish` still
writes the folder to upload by other means.

## Not yet

New pages and menus from the window, undo, the WebAssembly renderer release
and its reference from the shadow, the newsletter's e-mail mode of the
emitter, the Markdown block, and
everything of the server tier (Phase 2): forms and newsletter sign-ups on a
static site need a form service until then.
