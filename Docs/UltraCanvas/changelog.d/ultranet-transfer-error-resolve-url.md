- **An async HTTP transfer cut off after its status line looked like a
  success.** When a response went over `maxReceiveSize`, timed out or lost its
  connection mid-body, `UltraNet_HttpRequestAsync` handed its callback the
  server's status (200, say) and the bytes that had arrived, with nothing to
  tell them from a whole response; only the synchronous calls returned the
  error. `UltraNetResponse::transferError` now carries why the transfer did
  not finish (also for a cancelled request), empty when the whole response
  arrived, `IsComplete()` asks it, and `exceededReceiveLimit` marks the
  over-the-limit case. An HTTP error status (404, 500) is a
  finished transfer and leaves it empty.
- **`UltraNet_ResolveUrl(base, reference, out)`** (`UltraNetUrl.h`) resolves a
  relative URL against a base the way RFC 3986 section 5 does - `../img/a.png`,
  `//cdn.example/x`, `?page=2` - through libcurl's URL parser. UltraWeb uses
  it for an app's relative fetches. It refuses a URL with a control character
  in it (a CR or LF would end the request line) itself: libcurl 8 does, but
  libcurl 7.81 on Ubuntu 22.04 lets them through.
- Tests: `url_resolve_*` in `Tests/UltraNet/test_url.cpp` and
  `loopback_async_reports_an_incomplete_transfer` in
  `Tests/UltraNet/test_loopback.cpp`.
