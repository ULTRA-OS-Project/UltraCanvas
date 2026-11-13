- **Mail that names the system font drew in a stray serif or CJK face on
  Windows (LinkedIn's among them).** The HTML reader took the first name of a
  `font-family` list, and LinkedIn's starts with `-apple-system`, which no
  Windows or Linux system has; with the Windows system fonts in fontconfig,
  such a name fell to whichever font fontconfig listed first.
  - `HTMLStyleResolver`: `-apple-system`, `system-ui`, `BlinkMacSystemFont`
    and `ui-sans-serif` first in the list are the default (interface) font, as
    in a browser; `ui-monospace` is monospace, `ui-serif` serif.
  - The fontconfig configuration the framework writes (Windows, Android) gets
    fontconfig's `49-sansserif.conf` rule: a request naming no generic family
    has `sans-serif` appended, so any family nobody installed falls to the sans
    font.
  - The same rule draws a mail's web font (PayPal's `SupremeLLTest`, from an
    `@font-face` the reader does not load) in the sans font, not in a stray
    one.
- **HTML text is drawn in the first font of its `font-family` list that is
  installed**, as in a browser: `"Helvetica Neue", Arial, sans-serif` is
  Arial on Windows, PayPal's `SupremeLLTest, 'Avant Garde', Helvetica, Arial`
  is Arial, `Menlo, Consolas, monospace` is Consolas. The resolver keeps the
  whole list (`ComputedStyle::fontFamilyList`, a `<font face>` list too); the
  element builder picks from it; a list with nothing installed is the
  interface font. A list naming `monospace` is code, whatever its first name.
  - `IsFontFamilyAvailable` is cheap: the installed families are listed once
    per font set (`GetFontSetGeneration`), not on every call, and listed again
    when the Windows system fonts arrive after the start.
