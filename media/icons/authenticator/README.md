# UltraAuthenticator icons

The interface icons of `Apps/UltraAuthenticator`, loaded by name through
`Theme::IconPath` in `Apps/UltraAuthenticator/Theme.h`. All are drawn on a
16 px grid: one colour, a 1.5 px stroke with round caps and joins. A button
draws them as a mask in its text colour, so the colour in the file does not
matter.

- **Main window**: `scan` (Scan QR code), `key` (Enter key), `password`
  (Master password), `backup`, `restore`, `settings`, `lock`.
- **A card's actions**: `copy` (the code; `copied` for a moment after),
  `edit`, `show` (the setup key), `delete`, `next` (the next HOTP code).

The packaged app icon is `media/appicon/UltraAuthenticator.{png,svg}`, not here.
