- **UltraNet 0.3.0: downloads that resume, pause and are not cut off at
  30 seconds.** What UltraStore's package downloads need, and what every
  download so far lacked:
  - **`UltraNetHttpOptions::outputFilePath` works**, on the sync and the
    async path: the body goes to the file, `response.body` stays empty, and
    the file is closed before `onComplete` fires. `UltraNet_HttpDownloadFile`
    is now this with its `localPath`. It was declared and never read; the
    API status tool had it as NOT IMPLEMENTED.
  - **Range resume.** `resumeOutputFile` continues a partial file: its size
    is the offset, the request asks for `bytes=<size>-`, a 206 is appended,
    a 200 from a server that ignores the range replaces the file rather than
    being appended to it, and a 416 whose `Content-Range` total is the file's
    size means the file was already complete and is reported as success.
    `resumeFromOffset` does the same for a caller holding its own partial
    copy. Progress figures count the resumed bytes, so they are of the whole
    file.
  - **No whole-transfer timeout on a download.** A transfer that writes a
    file has no `timeoutMs` limit by default and a stall limit instead: the
    new `stallTimeoutMs` (the config default, 30 s, for a file; off
    otherwise) ends a transfer with `Timeout` when no byte has moved for that
    long. `timeoutMs = -1` turns the whole-transfer limit off for any
    request. Before this, `UltraNet_HttpDownloadFile` - the SDK download in
    UltraCanvasStart among others - failed after 30 seconds whatever the
    file's size.
  - **Pause and resume.** `UltraNet_PauseRequest` / `UltraNet_ResumeRequest`
    / `UltraNet_IsRequestPaused` on an async handle: nothing is read while
    paused, no callback fires, the connection stays open, the stall limit is
    suspended, and the transfer continues where it stopped. A pause asked
    for before the connection exists is applied as soon as it does.
  - **`httpsOnlyRedirects`.** A redirect to anything but `https://` ends the
    transfer as the new `UltraNetResultCode::RedirectRefused`, with the 3xx
    and its `Location` reported, so a download started over TLS is never
    bounced onto a plain connection. Without it a redirect may go to
    `http://` or `https://`; libcurl's default also allowed `ftp://`.
  - **`UltraNetHttpRequest::onHeaders`.** Fires once, when the final
    response's headers are complete and before its body - not for a redirect
    being followed or a 1xx - with `statusCode`, `headers`, `contentType` and
    `contentLength` filled. Returning false ends the transfer as `Cancelled`
    with nothing read: a `Content-Length` over the package's declared size
    is refused before a byte of it is downloaded.
  - Tested against a Range-capable loopback origin (`Tests/UltraNet/
    test_download.cpp`: 206, 200-start-over, 416-complete, the tail by
    offset, progress totals, the refused redirect, headers before the body
    and as a veto, a stall, pause and resume, an async file). The API status
    tool probes each of them (`/range/<n>`, `/redirect-http` and `/stall`
    routes on its origin).
