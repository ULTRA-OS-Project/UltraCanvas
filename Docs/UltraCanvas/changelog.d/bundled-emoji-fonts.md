- **Emoji ship with the framework, in colour or black and white.** The
  bundled fonts had no emoji, so on a computer without an emoji font - and on
  Windows, despite Segoe UI Emoji - an emoji drew as a box of hex digits: the
  👤 at the start of a LinkedIn subject in UltraMail, for one. Two fonts now
  ship in `media/emoji/` (SIL OFL 1.1; `THIRD_PARTY_LICENSES.md`):
  Noto Color Emoji 2.047 (CBDT, 10.8 MB) and Noto Emoji 3.006 (black and
  white, 0.9 MB).
  - New `UltraCanvasEmojiSettings.h` (doc `UltraCanvasEmojiSettings.md`):
    `EmojiStyle` Colour (default), Monochrome or System, kept in `Emoji.conf`
    in the UltraCanvas settings folder and chosen in UOS-Settings
    (Appearance > Emoji).
  - `ApplyEmojiSettings()`, called by `LoadBundledFontsNative` on Linux,
    Windows and Android and again when Windows switches to the full system
    font set, adds only the chosen font and a fontconfig rule that puts its
    family first for Pango's `emoji` runs. For black and white the rule also
    withdraws Pango's request for a colour font, which fontconfig weighs
    above the family. System loads Noto Color Emoji behind the system's own
    font. Applications started after a change use it.
  - The fonts are kept out of `media/fonts/`, so the WebAssembly pages do not
    preload them, and the macOS bundles leave `media/emoji/` out: macOS draws
    Apple Color Emoji.
  - The Windows fonts.conf alias for `emoji` ends with Noto Color Emoji.
  - Tested in the new `EmojiSettingsTest`: "👤 Someone" in Ubuntu Bold, with
    the bundled fonts alone, draws its emoji from the chosen font, with no
    missing glyph.
