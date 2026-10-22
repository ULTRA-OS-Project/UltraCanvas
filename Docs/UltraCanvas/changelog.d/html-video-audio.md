- **`<video>` and `<audio>` in the HTML reader.** `HTML::BuildOptions::mediaFactory`
  asks the host for a player with an `HTML::MediaRequest`: the element, its
  sources in order (`src`, then the `<source>`s with their types), its
  poster, `controls` / `autoplay` / `loop` / `muted`, and the size a browser
  gives it (CSS, else the attributes, else 640 x 360 or an audio bar of
  300 x 40). The player is placed as a picture is. Without a factory, or when
  it answers null, the fallback content between the tags shows, as before.
  `UltraCanvasWebView` sets the factory itself (`SetMediaPlayback`, on by
  default). Each element plays in an `UltraCanvasVideoPlayerElement` on the
  first address the page may load. The view keeps a player across rebuilds
  of the same page, so playback goes on, and stops it when the page changes.
  `autoplay` plays only a muted element (`Tests/HTMLMediaTest.cpp`).
- **`HTML::PageLoaderServices::fetchResource`** fetches a style sheet or a
  picture together with the address of the page it is for, when the host
  sets it; otherwise `fetch` is used as before. UltraWeb uses it to keep
  cookies from going to third parties. `PageLoader::AllowsResource` applies
  the loader's scheme rules (mixed content, local files) to an address the
  page loads outside the loader, and `PageUrl()` returns the address of the
  page on show.
- **`UltraNetCookieJar`, a browser's cookie jar in UltraNet**
  (`UltraNet/UltraNetCookieJar.h`). Unlike a session's libcurl cookies, the
  caller sees every cookie and decides when they go. `Store` applies a
  `Set-Cookie` header, and `HeaderFor` returns the `Cookie` header for a
  request, by RFC 6265bis: expiry (at most 400 days), `Domain` and `Path`,
  `Secure`, `SameSite` and the `__Secure-` / `__Host-` prefixes. A request
  for another site's page (`Classify`, `SiteOf`) neither sends nor sets one.
  The cookies with an expiry are kept in a tab-separated text file (UltraNet
  pulls in no JSON engine). UltraWeb is its first user; its tests are in
  `Tests/UltraNet/test_cookie_jar.cpp`.
- **GStreamer's base and good plugins are installed on the Linux CI row and
  in cloud sessions** (`.github/workflows/build.yml`,
  `.claude/hooks/session-start.sh`). Without `playbin` every video player
  opened nothing, so `HTMLMediaTest` could only check the fallback content;
  now it plays its WAV there.
