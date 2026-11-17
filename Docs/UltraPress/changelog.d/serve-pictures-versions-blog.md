- **`ultrapress serve <site>`** shows the site on this computer. It builds
  the site as HTML + JavaScript, serves it on 127.0.0.1, and builds it
  again when a file of the site changed; a built folder is served as it
  is.
- **Pictures** of the site's media are responsive: AVIF and WebP variants
  at the widths each role is shown at, with the original kept.
- **`--precompress`** on `publish` and `docs` writes a `.gz` (and `.br`)
  copy beside every page, stylesheet and script.
- **Versioned docs.** `versioned_docs/<label>/`, ordered by
  `versions.json`, is published at `/docs/<label>/` with a version
  switcher; `ultrapress docs --versions <dir>` does the same for a
  Markdown folder.
- **A blog.** Posts take `"tags"`. A site with posts gets `/blog/`, the
  posts newest first, and a page per tag.
