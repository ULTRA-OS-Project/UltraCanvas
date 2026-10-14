#### 2026-10-14 *0.7.2*
- **The FAQ's questions edit in place too.** A click on a closed question
  opens it as before; a click on an open one puts the field there, like
  every other piece of text on the page.
- **A paragraph field keeps its text when it is left.** Clicking elsewhere
  commits the area the way it commits a single line; the framework's text
  area gained the focus callbacks the text input had.
- **The shadow's pictures carry their size.** Every `<img>` of a media
  picture gets `width` and `height` from the file's header, and the
  social card its `og:image:width` and `og:image:height`, so a browser
  lays the page out before the pictures arrive (`HtmlOptions::
  siteDirectory`, which the publish and `ultrapress page` pass).

#### 2026-10-14 *0.7.1*
- **A picture's pixels.** The library reads a picture's size from its
  header - PNG, JPEG, GIF, WebP, BMP - without decoding it
  (`ImageDimensions`); the list and the dialog show "1600 × 900" beside
  the file size, and a picture on the page keeps its own shape: as wide as
  its frame allows and as tall as that makes it, instead of a fixed box.
- **`ultrapress media <site> prune`** removes every file no page uses,
  the pair of the publisher's `--prune`; the dialog has **Remove unused**.
- **One convention for media names.** The WordPress importer now writes
  attachments flat into `media/` under the library's clean names
  (`media/desktop.png`, a second `desktop.png` from another month
  `desktop-2.png`), as Add file… does, instead of `media/<year>/<month>/`;
  a site imported and one built in the editor read alike in the library.
  `CleanMediaName` is the one rule.

#### 2026-10-14 *0.7.0*
- **Text edited in place.** A click on a piece of text on the page - a
  title, a kicker, a heading, a paragraph, a caption, a quote and its
  source, a feature, an answer, a download's description - selects its
  block and puts a field where the text was: a single line for a title, an
  area for a paragraph. Enter (Ctrl+Enter in the area) keeps the text,
  Escape drops the change, and so does leaving a single-line field; the
  block is written, the page re-rendered, the properties pane and the block
  list follow. One paragraph of a long text is edited on its own and the
  others are kept. Behind it: the renderer, with `RenderOptions::editable`,
  puts every piece of block text in a slot of its own and records a
  `TextBinding` (the block's path, the property, the item and key, the
  paragraph) for it; the editor swaps the label for the field in that slot.
  The engine gained `SplitParagraphs` (the one way a long text becomes
  paragraphs, the shadow's too), `TextLocation` / `SetBlockText` and
  `ResolveBlockPath`. Rich text and HTML blocks keep their property pane.
  Tests: the model (paragraphs, locations, paths) and the editor's edit,
  cancel and commit without a window.

#### 2026-10-13 *0.6.0*
- **Pictures and the media library.** The site's `media/` folder has a
  model (`engine/UltraPressMedia.h`): `ListMedia` lists every file with
  its kind (from the extension), size and how many places in the site name
  it; `MediaReferences` finds those places - every picture property of
  every block, children included, a downloads item's file, a page's social
  card picture - and `ValidateMedia` reports the ones naming a file the
  site does not have, or outside `media/`; `ImportMedia` copies a file in
  under a clean name (the name slugged, the extension lower-case, `-2`
  when taken; a file already in the folder is named, not copied twice);
  `RemoveMedia` deletes one, never a used one, never anything outside
  `media/`. The command line: `ultrapress media <site>` lists, `media
  <site> add <file>...` copies in, `media <site> remove <path>...`
  deletes; `ultrapress check` reports missing pictures. The window: a
  picture property has **Pick…** beside its path, which opens the library
  to choose from - thumbnails, kind, size, uses - with **Add file…**
  (the file dialog; the file is copied into `media/`) and **Remove**; the
  side column's **Media library…** opens the same dialog to browse. The
  preview re-renders as pictures arrive. Tests: the library model (kinds,
  naming, references, validation, removal) and the editor's calls.

#### 2026-10-13 *0.5.1*
- **Pruning.** `ultrapress publish --to ... --prune` deletes on the host
  what an earlier publish put there and the site no longer has - a removed
  page's file and its folder once it is empty, innermost first - and
  nothing else: only what the manifest remembers uploading is ever touched.
  Without `--prune` such a file stays and is remembered, so a later prune
  finds it. The window's Publish prunes. A host that cannot delete (the
  in-memory demo provider) says what is left on it; FTP, WebDAV and
  Nextcloud can, WebDAV and Nextcloud having gained the verb in the
  framework today. For that to mean anything the static output now mirrors
  the site: `PublishStatic` removes from the output folder what an earlier
  publish wrote and this one does not (a removed page's folder, a picture no
  longer in `media/`; the deploy manifest stays), `removeStale` in
  `PublishOptions` turns it off. Run end to end against the local FTP and
  WebDAV servers: a page removed from the site left both hosts.
- **SFTP with a key.** `ultrapress hosts add ftp --server sftp://... --key
  ~/.ssh/id_ed25519` signs in with that private key, the password (when
  given) as its passphrase; the dialog behind Host… has the same row. The
  key file stays where it is; its path goes into the vault with the
  password.

#### 2026-10-13 *0.5.0*
- **The publisher onto web hosts.** A site is published through UltraCloud's
  providers - FTP / FTPS / SFTP, WebDAV, Nextcloud - onto a host the
  program keeps: `ultrapress hosts add ftp --server sftp://my.example
  --user me --folder /public_html` stores the account (verified against the
  server first; the password from `--password` or `ULTRAPRESS_HOST_PASSWORD`,
  into the credential vault under the settings folder, the list into
  `hosts.db` beside it), `ultrapress hosts` lists them, `hosts remove`
  forgets one, and `hosts use <site> <host>` writes the host and the folder
  into `site.json` as `hosting`. `ultrapress publish <site> <out> --to
  site` (or `--to <host>`, `--path <folder>`) then writes the static site
  and uploads it: the root's folders are created first, only the files
  whose fingerprint changed since the last upload to that host are sent
  (`.ultrapress-deploy.json` beside the output remembers them per host;
  `--force` sends everything), a file that fails does not stop the others,
  and three failures in a row - a host that is down - end the run with the
  rest left for next time. The window has **Publish**, which writes the
  site under `<site>/.ultrapress/publish` and uploads it on a worker thread
  with the status line following file by file, and **Host…**, UltraCloud's
  add-account dialog limited to the web hosts; the host added becomes the
  site's. Engine: `UltraPressDeploy.h` (`DeployFolder`, `Uploader`,
  `Fingerprint`, `JoinRemote`), headless and free of UltraCloud; the module
  side is `hosting/UltraPressHosting.h` (`Hosts`), target
  `UltraPressHosting`, built where UltraCloud is. Tests: the deploy through
  a recording uploader (first upload, nothing the second time, a changed
  page, another host, `--force`, a refused file tried again, a host that is
  down) and through the hosts onto UltraCloud's in-memory provider; run
  here end to end against a local FTP server and a local WebDAV server.

#### 2026-10-13 *0.4.1*
- **One program.** `ultrapress` is the commands and the editor window: a
  command word first (`new`, `check`, `page`, `publish`, `import`, `help`)
  runs the command, a site folder opens the editor, `--check` after the
  folder renders every page without a window. The separate `UltraPress`
  window binary is gone: the macOS build stopped with "No rule to make
  target `ultrapress.dir/depend`" because on a case-insensitive filesystem
  the two targets' names, and their binaries' paths, are one. The desktop
  entry and the packaging scripts name `ultrapress`; on macOS the app bundle
  is built from it, with the command-line tool beside it, as UltraFIBU's
  `ultrafibu` is one program for the same reason.

#### 2026-10-13 *0.4.0*
- **The WordPress importer.** `ultrapress import <export.xml> <site-dir>`
  reads a WordPress export (Tools > Export, the WXR file) into a site: the
  site's name, address, language, tagline and owner; every page and post
  with its slug, status (published, draft, scheduled), date and excerpt as
  its description; the front page (the page named home, or `--front`);
  the menus with their nesting, the first as `main`, one named footer as
  `footer`; and the bodies as blocks. The block editor's markers map onto
  the registry - paragraphs (plain ones merged into a text block, ones
  with links or formatting kept as rich text), headings, images with their
  alt and caption, covers as heroes, quotes with their source, buttons
  (a link into the site names the page), columns, galleries, lists,
  spacers, HTML - and every other block and classic content become rich
  text, with the report naming what was kept that way and the shortcodes
  left in classic text. Attachment URLs, sized variants included, are
  rewritten to `media/<year>/<month>/<file>`; `--media` fetches them
  through UltraNet, else they are listed. A post's dated WordPress address
  is reported as a redirect to its new path. Tested against a small export
  in `Tests/UltraPress/data`.

#### 2026-10-13 *0.3.1*
- **An icon of its own, and the app packaged everywhere.**
  `media/appicon/UltraPress.svg` - a page of blocks: the dark header band,
  the red hero, lines of text and a button - with its 256 px PNG; the
  window, the taskbar and the Windows executable carry it, the Linux
  install rules place it with `UltraPress.desktop`, and `package-macos.sh`
  builds the app bundle beside the command line.
- **The page and block lists are flush left.** The framework's button drew
  its text centred whatever `SetTextAlign` said (framework fix, see its
  changelog), so short labels sat in the middle of their buttons.

#### 2026-10-13 *0.3.0*
- **Editing.** The window's left column lists the current page's blocks
  with their first words; a block is selected, moved up or down, removed,
  or added after the selection from a drop-down of every registered type,
  with defaults that validate. The right column is the property pane,
  generated from the block registry: a text input for a line, a text area
  for paragraphs and HTML, a checkbox for a flag, an integer field for a
  number, and the links, items and lists as JSON in a text area that is
  applied as soon as it parses. With no block selected it edits the page's
  title, description, status and pinning. Every change goes into the
  content model and re-renders the preview; *Save site* writes the folder.
  The status line says what is unsaved, what was added, and when JSON is
  not valid yet. The editor's model side is tested without a window.

#### 2026-10-13 *0.2.0*
- **The block renderer on real elements, and the UltraPress window.**
  `Apps/UltraPress/render/UltraPressRender.h` turns a page of the content
  model into UltraCanvas elements the way the ultraos.eu front page demo is
  built - full-width bands whose centred column is Block-laid-out, wrapping
  Flex rows for columns and buttons - for every one of the seventeen blocks:
  the hero and call to action on the night colours, the FAQ as an accordion,
  forms with real text inputs, text areas and checkboxes that hand their
  fields to a callback, the newsletter with its address field, rich text and
  HTML through the HTML reader's element builder, pictures from the site
  folder with a captioned tile where one is missing. The theme's colours and
  font reach it through `PaletteFromTheme`. Links call back with the page's
  path or the URL; the header carries the main menu with its drop-downs,
  the footer its menu and the owner. `UltraPress <site-dir>` opens the site
  in a window - the page list on the left, the rendered page on the right,
  a click on a page link or a menu item switches - and `--check` renders
  every page without a window. The same renderer is what the Emscripten
  build will publish. Tests: `UltraPressRenderTests` (every block, the
  accordion, a form's fields, the theme, the example site). No editing yet.

#### 2026-10-13 *0.1.0*
- **The headless engine and the `ultrapress` command line: Phase 1 of the
  UltraPress proposal begins** (`Docs/Research/UltraPressDesignProposal.md`).
  A site is a folder of JSON files - `site.json`, `theme.json`, `menus.json`,
  `pages/<slug>.json` - and a page an ordered tree of blocks from a registry
  of seventeen predefined types (`heading`, `text`, `richtext`, `hero`,
  `picture`, `columns`, `card`, `features`, `cta`, `faq`, `quote`,
  `buttons`, `form`, `newsletter`, `downloads`, `html`, `spacer`). The
  validator names unknown blocks, missing properties, links to pages the
  site lacks and menu items that lead nowhere. The HTML emitter writes the
  plain, script-free **HTML shadow** of every page - decided on 2026-10-13
  as the one place HTML stays - with the title, description, canonical URL,
  social card tags, `WebSite`, `Article` and `FAQPage` structured data, the
  menus and every block's text, plus `sitemap.xml`, `robots.txt` and the
  RSS feed of the posts; the renderer's loader and the UltraWeb app are
  referenced when a publish names them. `ultrapress new` writes a starter
  site, `check` validates one, `page` prints a page's shadow and `publish`
  writes the static tier - pages, sitemap, robots, feed, `.htaccess` and the
  media - for any web host. `Apps/UltraPress/examples/ultraos` is the
  ultraos.eu front page as a site. Tests: `Tests/UltraPress`
  (`-DULTRACANVAS_BUILD_ULTRAPRESS_TESTS=ON`). No window yet: the editor
  comes with the element renderer.
