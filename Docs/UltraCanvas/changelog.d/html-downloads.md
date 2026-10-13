- **HTML reader: downloads.** A navigation that found no page
  (`PageKind::Other`) told its host only the address and type.
  - **Bytes and name:** `HTML::PageLoader` now hands over the bytes
    (`LoadedPage::body`) and the name to save the file as (`fileName`).
  - **`PageLoader::DownloadFileName`** picks that name:
    Content-Disposition's `filename*` (RFC 5987) or `filename`, else the
    address's last segment, cleaned of folders, control characters and
    leading dots, else `download` with an extension for its type.
  - **Attachments:** `Content-Disposition: attachment` makes even an HTML
    answer a download (`LoadedPage::attachment`); the fetch reports the
    header in `FetchResult::contentDisposition`.
  - **Large files:** a non-page answer over the fetch's size limit
    (`FetchResult::tooLarge`) is a download marked `truncated`, for the
    host to fetch again streaming, instead of an error page.
  - **`UltraCanvasWebView`:** a download keeps the page on show, its
    address and its history, which no longer gains an entry for the file.
  - Tests in `HTMLPageLoaderTest` and `WebViewTest`.
