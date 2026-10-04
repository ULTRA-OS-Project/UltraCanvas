- **The mail-connection trace: the documented variable, no passwords in it,
  and POP3 too.** `Docs/Modules/UltraNet/README.md` told readers to set
  `ULTRANET_CURL_DEBUG`, but the plug-ins read `ULTRANET_CURL_VERBOSE`, so
  the documented trace never turned on; the README now names the variable
  the code reads, and says what is redacted as it is printed
  (`<redacted auth line>`).
  - The trace masked only lines with "AUTH" in them, so the plain sign-ins
    libcurl falls back to when a server offers no SASL went out as they
    were: POP3's `PASS password` and IMAP's `A001 LOGIN user password`.
    Both are redacted now (`UltraNetCurlDebug.h` 0.2.0,
    `ultranet_curldebug::IsPlainLogin`); the server's replies are still
    kept whole.
  - The POP3 plug-in (0.1.2) honours `ULTRANET_CURL_VERBOSE` like the SMTP
    and IMAP ones; the README listed it, but it never asked.
  - `Tests/UltraNet/test_curl_debug.cpp` checks the redaction rule.
