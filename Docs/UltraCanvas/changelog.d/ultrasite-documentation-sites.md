- **UltraSite publishes documentation sites.** It builds them the way
  Docusaurus does, without React, hydration or a bundler.
  - **Loading.** `Builder::AddDocs` reads a folder of Markdown: a page per
    file, a folder's `index.md` or `README.md` as the folder's page, and a
    generated page for a folder without one. It reads front matter
    (`title`, `description`, `slug`, `sidebar_label`, `sidebar_position`,
    `draft`) and `_category_.json`. A link to another `.md` becomes that
    page's address, a file in the folder becomes an asset, and a file
    outside it goes to a repository's file view. Translations are joined
    page by page.
  - **The `doc` layout.** A sidebar in which only the categories leading
    to the page are expanded, a table of contents, breadcrumbs, previous
    and next, and *Edit this page*. On a phone the sidebar opens without
    script.
  - **Markdown** (`UltraSiteMarkdown.h`). Heading ids are spelled as
    GitHub spells them, so a repository's anchors keep working. Code is
    highlighted when the site is built. There are `:::note` / `:::tip` /
    `:::warning` admonitions, GitHub's `> [!NOTE]` alerts, and `:::tabs`.
    Wrapped lines are joined into paragraphs.
  - **Search.** An index per language and a search page that lists every
    page without script. An index over its budget keeps titles, headings
    and descriptions only.
  - **New behaviour modules:** `search`, `toc`, `tabs` and `copy`. A
    language switcher, and the frame's words in `Labels`.
  - **The build reads each page back once,** not three times.
    `BuildOptions::brokenLinksWarn` makes a broken link a warning, as
    Docusaurus's `onBrokenLinks: 'warn'` does.
- **`UCRichDocument::ToHTML` gains two hooks for web pages** in
  `RichDocumentHTMLOptions`. `headingId` gives each heading an `id`, and
  `codeBlock` writes a code block's HTML itself, highlighted, say. Unset,
  the output is what it was.
