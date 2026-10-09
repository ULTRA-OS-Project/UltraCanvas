- **`ultramsg` has an icon.** The uploaded UltraMsg logo is
  `media/appicon/UltraMsg.svg` now (it arrived as `UltraMsg logo.svg`; an
  icon name with a space cannot be looked up in an icon theme), and
  `media/appicon/UltraMsg.png` is its 256 px render through librsvg, as for
  the other applications. The UltraMessage command line has no window, so
  the one place it shows is the Windows `ultramsg.exe`, which embeds it
  (`ultracanvas_embed_app_icon`).
