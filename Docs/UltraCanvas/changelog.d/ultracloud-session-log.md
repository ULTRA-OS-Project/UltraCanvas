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
- **A throttled cloud request is sent again after the wait the service
  asks for.** `HttpProviderBase::Send` used to return the first 429 as the
  failure. It now waits as long as `Retry-After` says - or 1, 2, 4 s when
  the service gives no time - and sends the same request again: the service
  did not act on it, so this is safe for every method, uploads included.
  `RateLimitPolicy` (`UltraCloudHttp.h`), set per thread with
  `SetThreadRateLimitPolicy`, says how many tries (3 after the first by
  default), the longest wait sat out (30 s by default; a longer one fails at
  once as `RateLimited`) and how to wait - a function a worker can return
  `false` from to stop waiting when it is shut down; the default sleeps.
  Each wait, and a request given up, is a line in the thread's log.
- **The browser sign-in says its steps.** `OAuthProviderBase::SignIn` logs
  where it waits for the browser's answer, the sign-in page it opens
  (without its query: the one-time state and code challenge stay out), and
  whether the service gave an access token or why the sign-in did not
  complete.
- **The add-account dialog can be watched.** `ShowAddAccountDialog` takes
  an optional `AddAccountWatch`: `onBegin` and `onEnd` run on the dialog's
  sign-in thread around each sign-in, so a host can set that thread's log
  sinks and record the attempt - UltraFiler puts it in its connection log.
- `UltraCloudTests`: a throttled request sent again and answered, Google
  Drive's 403 waited out 1, 2 and 4 s and then given up, a wait longer than
  the policy allows, a wait interrupted, and the browser sign-in's lines
  without its state, code challenge or tokens - with a recorded wait, so no
  test sleeps.
- **A rejected access token is renewed and the call made once more.**
  `CloudService` renewed an OAuth token only when it had expired by the
  local clock; a 401 for one the service rejected anyway - revoked early, a
  clock that is off, or expired while `Send` waited out a service limiting
  requests - failed the call. Every `CloudService` call now runs through
  one helper (`Run`) that, on `AuthFailed` with HTTP 401 for an account
  with a refresh token, renews the token, stores it and makes the call once
  more - not when the token was renewed a moment before, and never more
  than once. `UploadAndShare` runs its three steps again as a whole.
- **A sign-in can be called off.** `UltraNet_OAuth2WaitForCallback` and
  `UltraNet_OAuth2AuthorizeInteractive` take an optional `isCancelled`,
  asked about five times a second while the loopback listener waits for
  the browser; once it says true the wait ends as
  `UltraNetResultCode::Cancelled` instead of holding the thread for up to
  five minutes. UltraCloud's per-thread `SetThreadSignInCancel` /
  `SignInCancelled` (`UltraCloudProvider.h`) feeds it: an OAuth sign-in
  ends as the new `ResultCode::Cancelled` (after `RateLimited`; the other
  codes keep their numbers), and `CloudService::AddAccount` /
  `SignInAccount` store no account once the check says true. The
  add-account dialog's *Cancel* and close box call a running sign-in off -
  before, a sign-in the user went on to finish in the browser was stored
  after the dialog had been cancelled - and `AddAccountWatch::cancelled`
  lets a host call it off as well.
- Tests: the redirect wait called off (`test_oauth2.cpp`); a rejected token
  renewed and the call made again, renewed once and not in a loop, a
  refused renewal, a token renewed a moment ago not renewed again, a
  cancelled sign-in storing no account and a browser sign-in called off
  (`test_log.cpp`).
