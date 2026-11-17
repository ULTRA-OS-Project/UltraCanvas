- **UltraSite: responsive pictures, compressed copies, a preview server and
  versioned docs.**
  - **Pictures.** `Builder::Picture` writes a picture as a `<picture>`
    with AVIF and WebP variants at the widths below its own. The variants
    are named by the picture's content hash, so a later build reuses them.
    The original is the `<img>`, with its size. A format this machine's
    image library cannot write is left out, and the build says so.
  - **Compressed copies.** `BuildOptions::precompress` writes `.gz` copies
    and, where the build finds Brotli, `.br` copies of the pages,
    stylesheet, scripts and lists, for nginx's `gzip_static` /
    `brotli_static` and Caddy's `precompressed`. Brotli is an optional
    dependency (`libbrotlienc`).
  - **The preview server.** `UltraSiteServer.h` serves a built folder on
    127.0.0.1, the way a host would: the site's path, the trailing-slash
    redirect, the 404 page, the cache rules and the compressed copies.
    `Respond` answers one request without a socket; `Serve` listens
    through UltraNet.
  - **Versioned docs.** `DocsOptions::versions` gives each earlier version
    its own prefix and sidebar, a version switcher, and a banner leading
    to the latest. The search finds only the latest.
  - `BasePathOf(url)` gives the path a site is served from.
