- **UltraCloud's providers log their requests, and a refusal keeps the
  service's reason.** A failed Dropbox, OneDrive, Google Drive, Nextcloud or
  WebDAV call came back as `HTTP 409` or `HTTP 403` with nothing else: the
  body that said why (`path/not_found`, `userRateLimitExceeded`,
  `invalid_grant`) was dropped, and there was no way to see which requests
  had been made. A caller - UltraFiler's connection log - had a session log
  for FTP drives only.
  - `UltraCloudLog.h` (new): a per-thread sink, `SetThreadLog`, the way
    `UltraNet_SetThreadFtpLog` serves the FTP provider. Every request through
    `HttpProviderBase::Send` writes a *Request* line (method and URL) and a
    *Response* line (status, the service's reason when it refused, how long
    it took), or an *Error* line when no answer came. `CloudService` logs an
    access token being renewed and a renewal refused; the Dropbox, OneDrive
    and Google Drive listings log each further page. With no sink set,
    nothing is formatted.
  - Nothing secret reaches a line: no header (the bearer token among them),
    no body, and `LoggableUrl` masks the value of every query parameter
    whose name carries a secret (`tempauth`, `key`, `code`, `sig`, ...) and
    drops a URL's user name and password.
  - `ServerErrorReason` reads each service's error body - Dropbox's
    `error_summary`, Microsoft Graph's `error.code` / `message`, Google's
    `errors[].reason`, OAuth's `error` / `error_description`, and the
    WebDAV / SabreDAV `<s:message>` (`WebDavErrorMessage`, new) - and
    `HttpProviderBase::FromHttp` puts it in the message:
    `list /Photos: HTTP 409 - path/not_found`, `... sign-in rejected
    (HTTP 401 - ...)`, `... not allowed (HTTP 403 - ...)`.
  - A throttled request - 429, a 503 with `Retry-After`, or Google Drive's
    403 `rateLimitExceeded` / `userRateLimitExceeded` - is the new
    `ResultCode::RateLimited` (added after `Unknown`; the other codes keep
    their numbers), and its message says how long the service asks to wait
    (`RetryAfterSeconds` reads both forms of `Retry-After`).
  - A failure's `diagnostics` carries the HTTP status, the service's
    reason, `Retry-After`, the request ID the service's support asks for,
    the URL that answered and the time taken, then the transport's
    connection chain.
- **A failed UltraNet HTTP transfer carries its diagnostics.** A request
  that got no answer (no route, TLS refused, timed out) filled
  `UltraNetResult::message` only; it now fills `diagnostics` with the
  connection chain (`UltraNetCurlError.h`), as FTP and mail failures
  already did.
- `UltraCloudTests` gains `test_log.cpp`: URL masking, every service's error
  body, `Retry-After`, the throttled OneDrive 429 and Google 403, a sign-in
  refusal, a Nextcloud server that never answered, the Dropbox log lines
  without the token, and a token renewal succeeding and refused.
