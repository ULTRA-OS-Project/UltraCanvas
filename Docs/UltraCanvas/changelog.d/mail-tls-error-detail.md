- **IMAP, SMTP and POP3 errors now say *why* a connection failed.** They used
  to report only curl's error class — for a rejected server certificate that
  was "SSL peer certificate or SSH remote key was not OK", which does not say
  whether the certificate is self-signed, expired, issued for another host
  name or missing its intermediate. The plug-ins now keep curl's
  per-transfer reason ("SSL certificate problem: unable to get local issuer
  certificate", "... certificate has expired", ...) in
  `UltraNetResult::message`, which UltraMail shows under its summary. This is
  the only trace a Windows GUI build leaves, since it has no stderr for
  `ULTRANET_CURL_VERBOSE`. Helper: `ultranet_curlerror::Perform`
  (`UltraNet/UltraNetCurlError.h`, header-only).
- **POP3 over TLS trusts the same CA anchors as IMAP and SMTP.** The POP3
  plug-in never set the CA bundle or, on Windows, the system certificate
  store, so on Windows every `pop3s://` sign-in was left with the libcurl
  build's own (non-existent) CA path.
