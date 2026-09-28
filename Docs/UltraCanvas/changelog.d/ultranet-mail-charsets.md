- **UltraNet decodes mail in every charset, not just UTF-8 and Latin-1.**
  `UltraNet_MimeDecodeHeader`, `UltraNet_MimeParse` and
  `UltraNet_MimeGetDisplayBody` passed any other charset through as raw
  bytes. Japanese mail in ISO-2022-JP therefore showed as
  `$B3t<02q<R%F%l%7%"(B` where "株式会社テレシア" was meant, and Shift_JIS,
  GB2312, EUC-KR, KOI8-R and windows-1251 text was garbled the same way.
  - Text in any charset iconv knows is now converted to UTF-8. Common mail
    labels are mapped to the names iconv expects (for example `x-sjis`,
    `ks_c_5601-1987`), and a charset that fails strictly is retried with its
    Windows superset (CP932 for Shift_JIS, CP50221 for ISO-2022-JP, GB18030
    for GB2312).
  - A byte that cannot be converted becomes U+FFFD, and the rest of the text
    is still converted.
  - Labels iconv does not know under that name are mapped as well: the
    Hebrew/Arabic `iso-8859-8-i` / `-e` variants, `unicode-1-1-utf-7`,
    `x-mac-roman`, `x-mac-cyrillic`, `hz-gb-2312`, `tis-620` /
    `windows-874`.
  - A charset nobody can convert (`unknown-8bit`, `x-user-defined`, a typo)
    no longer passes its bytes through as invalid UTF-8. Text that is
    already UTF-8 is kept, and anything else is read as windows-1252.
  - Header text written in ISO-2022-JP without encoded-words (older Japanese
    mailers put the JIS escape sequences straight in the header) is
    recognised and converted too.
  - `windows-1252` now maps 0x80–0x9F correctly (€, „, …) instead of treating
    it as Latin-1.
  - Build: UltraNet links `Iconv::Iconv` when CMake finds it (part of glibc,
    libiconv on macOS, and MSYS2's `libiconv` on Windows, which CI already
    installs) and defines `ULTRANET_HAS_ICONV`. Without iconv the old UTF-8 /
    Latin-1 behaviour remains.
  - Tests: `Tests/UltraNet/test_mime.cpp`, with the real ISO-2022-JP sender
    name above.
