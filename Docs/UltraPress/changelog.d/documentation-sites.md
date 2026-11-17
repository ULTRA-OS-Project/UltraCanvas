- **Documentation, the way Docusaurus publishes it.**
  - **A site's `docs/` folder.** Its Markdown is published at `/docs/` in
    the HTML + JavaScript output. Each page gets a sidebar after the
    folders, a table of contents, breadcrumbs, and previous and next.
    Code is highlighted, and `:::tip` admonitions and `:::tabs` work.
  - **Search.** A site with docs gets a search box and a search page.
  - **Translations.** `i18n/<language>/docs` publishes at
    `/<language>/docs/`, with a language switcher.
  - **The example site** has a small `docs/`, linked from its menu.
- **`ultrapress docs <folder> <out>`** publishes a folder of Markdown alone
  as a documentation site.
  - `--source-root` and `--source-url` send a link out of the folder to a
    repository's file view.
  - `--edit-url` adds *Edit this page*, and `--exclude` leaves out files
    such as the changelogs.
  - `--broken-links warn` publishes despite links that lead nowhere.
  - This repository's `Docs/` (418 pages) builds with it in 18 seconds.
