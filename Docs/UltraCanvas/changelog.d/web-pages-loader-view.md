- **`HTML::PageLoader`: a URL to a page the HTML reader can show**
  (`HTMLReader/HTMLPageLoader.h`). It fetches the document and reads its
  charset from the BOM, the Content-Type or a `<meta charset>`, else as UTF-8
  or Windows-1252. It applies `<base href>`. It fetches the linked style
  sheets before the page is ready, with each sheet's `url()`s made absolute
  against the sheet. It gives the builder its other resources on demand, from
  a cache, and says when more arrived. Plain text and pictures become pages
  of their own, and anything else goes back to the caller.
  - Rules and limits: an https page loads no http resources, a web page no
    local files, and a page loads at most 32 style sheets and 512 resources
    of up to 16 MB.
  - A navigation left before it answers never delivers.
  - Fetching, URL resolution and charset conversion are services the caller
    passes in, so the loader is framework-independent and runs offline in
    `Tests/HTMLPageLoaderTest.cpp`.
- **`UltraCanvasWebView`: a web page as an element.** It loads through the
  page loader and builds with the element builder into a scrolling pane.
  - Links: a `#fragment` scrolls, and other schemes go to `onExternalLink`.
    A click is followed on the next turn of the UI loop, never inside the
    label's own handler.
  - History: back and forward keep each page's scroll position.
  - It builds again on a new width and when pictures arrive, never from
    inside layout. A page that cannot be loaded shows an error page.
  - No JavaScript: `<noscript>` is shown.
  - `Tests/WebViewTest.cpp` covers it, and UltraWeb 0.3.0 is the browser
    around it.
- **`UltraNet_ConvertToUtf8`**: the charset conversion mail bodies already
  used (every iconv charset where the build has it), now public, for web
  pages.
- **`HTML::EscapeText`**, the inverse of `DecodeEntities`.
- **`<template>` is not rendered** by the HTML reader (its content is
  inert, as in browsers).
