- **A failed mail connection reports its whole chain.** `UltraNetResult`
  gains `diagnostics`: one "Name: value" line each for the error (curl's own
  reason with its libcurl error number), the component and its version
  ("UltraNet IMAP plug-in 0.2.0"), the server URL (credentials removed), the
  address actually connected to, the TLS mode (implicit, STARTTLS required or
  if offered, none), the sign-in (method, curl login options, kind of
  credential, user name - never the password or token), libcurl's version and
  target, every TLS backend compiled in (the active one without parentheses),
  zlib, the trusted roots and the operating system with its version and
  architecture. The IMAP, SMTP and POP3 plug-ins fill it
  (`ultranet_curlerror::Perform` / `Error` / `CurrentContext` in
  `UltraNet/UltraNetCurlError.h`, `ultranet_curlmailauth::RecordContext`); two
  new core functions supply the last two lines, `UltraNet_DescribeTrustRoots()`
  and `UltraNet_DescribePlatform()` (Windows via `RtlGetVersion`, which does
  not lie to unmanifested programs the way `GetVersionEx` does).
