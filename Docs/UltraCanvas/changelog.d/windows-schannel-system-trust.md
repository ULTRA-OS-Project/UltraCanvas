- **Windows: TLS verifies against the Windows certificate store.** The MSYS2
  libcurl the Windows packages ship runs on Schannel, not OpenSSL, and UltraNet
  handed it the bundled `cacert.pem` - with a CA file, curl's Schannel verifies
  against that file only, following the chain exactly as the server sent it.
  A Let's Encrypt mail server still sending its chain through the retired
  "DST Root CA X3" then failed in UltraMail with "the certificate or
  certificate chain is based on an untrusted root", although Windows itself
  (Outlook, Edge) trusts it through ISRG Root X1; so did any server whose root
  Windows trusts but the bundle lacks (an organisation's CA, a security
  suite's). Under Schannel UltraNet now gives no CA file and Windows' own chain
  building decides; `cacert.pem` is still used by an OpenSSL build. The active
  backend is read from `curl_version_info()` (a multi-SSL build marks the
  inactive ones with parentheses).
- **Windows: revocation is checked the way browsers do.** Schannel's default
  refuses a certificate when its revocation server cannot be reached; every
  UltraNet handle - the HTTP client and the IMAP, SMTP and POP3 plug-ins - now
  sets `CURLSSLOPT_REVOKE_BEST_EFFORT` beside `CURLSSLOPT_NATIVE_CA`, which
  still refuses a certificate that is actually revoked. One place for both:
  `ultranet_curltls::Apply` (`UltraNet/UltraNetCurlTls.h`, header-only).
