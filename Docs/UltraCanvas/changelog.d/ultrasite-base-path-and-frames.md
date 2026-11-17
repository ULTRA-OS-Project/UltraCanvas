- **UltraSite: a site under a path, and a frame per language.**
  - **A path.** A site whose `url` has a path (`https://store.example/v1`)
    is served from it, as Docusaurus's `baseUrl` is. Routes keep their
    site paths, and every address the builder writes carries the path:
    assets, navigation, search, the 404 page, `.htaccess` and `_headers`.
    `Builder::Url` does the same for an app's own HTML, and the link check
    reads the addresses back in that path.
  - **Shared folders.** With `removeStale` off, the files a folder already
    holds count as link targets, for a site that shares its folder (a
    store's catalog and media).
  - **Frames.** `SiteConfig::frames` gives another language its own
    navbar, footer and words.
