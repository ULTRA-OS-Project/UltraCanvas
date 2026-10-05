- **UltraNet's FTP calls report their session as it happens, and a failure
  says what actually went wrong.** A listing that hung after PASV used to fail
  with "Timeout was reached" and nothing else - no address, no reply, no word
  on the step it got to. Every `UltraNet_Ftp*` call now logs the lines an FTP
  client shows in its message log (`UltraNetFtp.h`): the steps (*Resolving
  address of ...*, *Connecting to 203.0.113.7:21...*, *Logged in*,
  *Retrieving directory listing...*), every command sent - with the password
  masked, `PASS ********` - every reply with its three-digit code, and an
  error line carrying UltraNet's result code and libcurl's error number. A
  caller passes `UltraNetFtpOptions::onLog`; one that reaches UltraNet
  through a layer building the options itself sets a per-thread sink with the
  new `UltraNet_SetThreadFtpLog`. The kind is `UltraNetFtpLogKind::Step`, not
  `Status`, which Xlib `#define`s. The transcript logic is curl-free in
  `core/UltraNet/UltraNetFtpLog.h`.
  - A failure's `message` is libcurl's specific reason (its error buffer)
    rather than the error class, with the server's refusal added when the
    last reply was a 4xx / 5xx ("RETR response: 550 - the server said
    \"550 Permission denied\""); `diagnostics` now carries the connection
    chain (`UltraNetCurlError.h`) and the last server reply for FTP too.
  - `UltraNetFtpOptions::inactivityTimeoutMs` (default 30 s) ends a call
    whose server has gone quiet - no reply to a command, no bytes of a
    listing or file - as "Connection timed out after N seconds of
    inactivity". Before it, a data connection that opened and then carried
    nothing held the call indefinitely.
  - A listing tries LIST and NLST after MLSD only when the server refused the
    command. A failure to connect, sign in, set up TLS or the data
    connection, or a timeout, is reported once instead of three times (a
    refused password was sent three times, and one timeout became three),
    and an empty folder is listed with one request instead of three.
  - `CURLE_REMOTE_FILE_NOT_FOUND` maps to `NotFound`, `CURLE_USE_SSL_FAILED`
    to `TlsHandshakeFailed`. `UltraNetResult::url` of an FTP call no longer
    carries credentials written into the URL.
  - `Tests/UltraNet/test_ftp_log.cpp` checks the transcript and drives real
    calls against a scripted FTP server on loopback (a refused sign-in, an
    empty folder, MLSD refused, a stalled data connection, a server that
    never answers the listing); `UltraNetApiStatus` probes
    `UltraNet_SetThreadFtpLog`.
  - The connection steps read every libcurl's wording: "Connected to" (7.x),
    "Connected 2nd connection to" (8.x) and, from 8.21, "Established
    connection to" / "Established 2nd connection to". The last is all the
    vendored `third_party/curl` (8.21, used where the system libcurl lacks
    WebSockets, as on Ubuntu 22.04) says, so there neither "Connection
    established" nor "Data connection established" ever appeared.
- **UltraCloud: a failed `Result` carries the transport's diagnostics.**
  `Result::diagnostics` holds the connection chain the provider's transport
  put together; the FTP provider fills it from `UltraNetResult::diagnostics`
  (`FromFtp`). UltraFiler's connection log shows it under each failure.
