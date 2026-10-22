# UltraCanvasEmojiSettings

Which emoji every UltraCanvas application draws, and the two emoji fonts the
framework ships for them. Header: `UltraCanvas/include/UltraCanvasEmojiSettings.h`.

## Contents

- [The choices](#the-choices)
- [Where it is kept](#where-it-is-kept)
- [How it reaches the text](#how-it-reaches-the-text)
- [API](#api)

## The choices

| `EmojiStyle` | Font | Notes |
|---|---|---|
| `Colour` (default) | Noto Color Emoji 2.047 (`media/emoji/NotoColorEmoji.ttf`, CBDT, 10.8 MB) | The same pictures on every computer. |
| `Monochrome` | Noto Emoji 3.006 (`media/emoji/NotoEmoji-Regular.ttf`, outlines, 0.9 MB) | Drawn in the colour of the surrounding text. |
| `System` | The system's emoji font | Segoe UI Emoji on Windows, the one fontconfig prefers on Linux; Noto Color Emoji is loaded behind it for what it lacks. |

The user chooses in UOS-Settings, *Appearance > Emoji*.

## Where it is kept

`Emoji.conf` in the UltraCanvas settings folder (`UltraCanvasSettingsFolder()`:
`%APPDATA%\UltraCanvas`, `~/Library/Application Support/UltraCanvas`,
`$XDG_CONFIG_HOME/UltraCanvas`), one line:

```
style=monochrome        # colour | monochrome | system
```

A missing file, or a value the reader does not know, is `Colour`.

## How it reaches the text

Each platform's `LoadBundledFontsNative` calls `ApplyEmojiSettings()` at
start, and the Windows switch to the full system font set calls it again for
the new configuration. It reads `Emoji.conf`, adds the style's font to the
current fontconfig configuration as an application font (only that one - the
fonts are kept out of `media/fonts/` for this reason) and loads the style's
rule:

- Pango asks for the family `emoji` and for a colour font on a run of emoji.
  The rule puts the style's family first (`prepend_first`, ahead of what the
  system's own `60-generic.conf` prefers).
- For `Monochrome` it also withdraws the request for colour, which fontconfig
  weighs above the family - otherwise an installed colour font would still
  win.

Noto Color Emoji is a bitmap font: its glyphs are 109 px pictures that
fontconfig has to scale to the size of the text. A system `fonts.conf` does
that through its `conf.d/10-scale-bitmap-fonts.conf`; the `fonts.conf` the
framework writes where there is none (the packaged Windows and Android
builds) carries the same rules, and `GenerateBitmapScalingConfig()`
(`UltraCanvasApplication.h`) hands them to a configuration made another way.
Without them every colour emoji is drawn 109 px high, whatever the size of
the text around it.

A change applies to the applications started after it. Where the text engine
is not fontconfig the call does nothing: macOS draws Apple Color Emoji (its
bundles leave `media/emoji/` out), and the WebAssembly pages do not preload
the fonts.

## API

```cpp
#include "UltraCanvasEmojiSettings.h"

void ChooseBlackAndWhiteEmoji() {
    UltraCanvas::EmojiSettings settings = UltraCanvas::EmojiSettings::Load();
    settings.style = UltraCanvas::EmojiStyle::Monochrome;
    if (!settings.Save()) {
        // the settings folder is not writable
    }
    // What an application started now loads, and the rule it applies:
    const std::string file  = UltraCanvas::BundledEmojiFontFile(settings.style);   // .../media/emoji/NotoEmoji-Regular.ttf
    const std::string rules = UltraCanvas::EmojiFontconfigRules(settings.style);   // a <fontconfig> document
    (void)file;
    (void)rules;
}
```

| Function | What it does |
|---|---|
| `EmojiStyleKey(style)` / `ParseEmojiStyle(key, out)` | The style as the file writes it (`colour`, `monochrome`, `system`) and back. |
| `EmojiSettings::Parse(text)` / `Serialize()` | The file's text and back. |
| `EmojiSettings::FilePath()` / `Load()` / `Save()` | `Emoji.conf`; `Save` writes beside it and renames over it. |
| `GetBundledEmojiDir()` | `media/emoji/` in the resources dir. |
| `BundledEmojiFontFile(style)` | The bundled file the style loads. |
| `EmojiFontFamily(style)` | `"Noto Color Emoji"`, `"Noto Emoji"`, or `""` for `System`. |
| `EmojiFontconfigRules(style)` | The rule, as a `<fontconfig>` document; empty for `System`. |
| `ApplyEmojiSettings()` | Reads the setting and applies it to the current fontconfig configuration, once per configuration. |

Tested in `Tests/EmojiSettingsTest.cpp`: the file round trip, and "👤 Someone"
laid out in Ubuntu Bold with only the bundled fonts drawing its emoji from the
style's font with no missing glyph.
