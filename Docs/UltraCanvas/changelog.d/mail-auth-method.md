- **Mail sessions take an authentication method.** `UltraNetMailOptions::auth`
  (`UltraNetMailAuth`, `UltraNet/UltraNetPlugins.h`) restricts an IMAP, SMTP
  or POP3 sign-in to one family - `Password`, `EncryptedPassword` (CRAM-MD5 /
  DIGEST-MD5, POP3 APOP), `OAuth2` (XOAUTH2 / OAUTHBEARER), `Kerberos`
  (GSSAPI), `NTLM` - or skips it (`None`, for a relay that trusts the
  network). `Any`, the default, is the old behaviour: whatever the server
  offers. The three plug-ins now share one sign-in routine,
  `ultranet_curlmailauth::Apply` (`UltraNet/UltraNetCurlMailAuth.h`,
  header-only), which also refuses up front a method that cannot work with
  the credentials given (OAuth2 without a token, a token with a password
  method) instead of letting the server reject it.
  - POP3 now signs in with an OAuth2 token too (it only ever sent a
    username and password).
