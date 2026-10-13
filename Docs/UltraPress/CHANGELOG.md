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
