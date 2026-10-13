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
