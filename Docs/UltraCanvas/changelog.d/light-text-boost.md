- **Text in a dark window is as firm as the desktop's own.** A dark
  UltraFiler beside a dark Explorer read thinner and greyer in the same
  font. Cairo blends a glyph's edge in sRGB numbers, not in light - a pixel
  white covers by half on black comes out 128 where 188 should arrive - so
  dark text on white gains weight and light text on a dark ground loses it;
  DirectWrite, ClearType and Core Text correct for that, and now the render
  context does: text whose colour is light is drawn a second time, which
  lifts a half-covered pixel to three quarters (191) and leaves a covered
  one as it is. On by default in every application, under any theme
  (`RenderContextCairo::SetLightTextBoost`, 1 by default, 0 for every text
  once; `ULTRACANVAS_LIGHT_TEXT_BOOST=0` for one run). No advance, extent
  or layout changes. Dark text, translucent text, text painted with a
  pattern and a PDF, PostScript or SVG page are drawn as before. Test:
  `LightTextBoostTest`. Docs: *Light text on a dark ground* in
  `UltraCanvasTextRenderingExamples.md`.
- **The dark theme's text is white.** `UltraCanvasTheme::Dark()`'s `ink`
  was 235 grey, which read greyer than a native dark window beside it; it
  is white now, as the Windows and GNOME dark looks write their text. The
  muted (190) and faint (150) inks are unchanged.
- **Light text, firm or plain, is a setting.** `Appearance.conf` gains
  `lighttext=firm | plain` (`AppearanceSettings::lightText`,
  `HostAppearance::firmLightText`, `lighttext=` in
  `ULTRACANVAS_HOST_APPEARANCE`); the application hands it to the render
  context when it changes and redraws, so a running application follows
  UOS-Settings within a moment. `ULTRACANVAS_LIGHT_TEXT_BOOST` still decides
  for its run.
- **DemoApp's Text Rendering page shows it.** A *Light Text* option (Plain,
  Half, Firm) and a card of light text on a dark ground in the dark theme's
  three inks.
