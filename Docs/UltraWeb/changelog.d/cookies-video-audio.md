- **Cookies: a site can keep you logged in.** UltraWeb sent and kept no
  cookies, so every login was forgotten by the next page. Now what a
  server's `Set-Cookie` stores goes back with the next request to its site:
  the page, its style sheets and pictures, a form's POST, and a large
  download. The rules are RFC 6265bis (`Expires` / `Max-Age` up to 400 days,
  `Domain`, `Path`, `Secure`, `SameSite`, the `__Secure-` / `__Host-`
  prefixes). There are **no third-party cookies**: a picture from another
  site neither sends nor sets one. Redirects are followed one hop at a time,
  so the cookie a login's redirect sets goes with the next request. Cookies
  with an expiry are kept in `<settings folder>/UltraWeb/cookies.txt`, and
  session cookies end when UltraWeb quits. **Ctrl+Shift+Delete** clears them
  all. The rules are UltraNet's `UltraNetCookieJar` (framework changelog:
  `html-video-audio`).
- **Video and audio play on pages.** `<video>` and `<audio>` show the
  framework's video player, with play / pause, seeking and volume. It plays
  http(s) streams (an https page plays no http stream), and local files on
  a local page. The player keeps playing when the page is built again (a new
  width, pictures arriving) and stops when the page is left. `autoplay`
  starts only muted clips; sound waits for a click. Without a media
  framework, or for a source it cannot open, the element's fallback content
  shows instead (framework changelog: `html-video-audio`).
- **`UltraWeb --page`** keeps the cookies of its one load, so a site that
  sets a cookie and redirects shows its page instead of a redirect loop.
- **Plan: calls through UltraRTC.** `Docs/UltraWeb/UltraWebRTCIntegration.md`
  sets out how UltraRTC reaches UltraWeb: WebSockets and `uc_rtc_*` imports
  for WebAssembly apps first, the JavaScript `RTCPeerConnection` once pages
  run scripts.
