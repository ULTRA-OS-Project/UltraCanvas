- **Colour emoji were drawn huge on Windows.** The bundled Noto Color Emoji,
  the default emoji font since the emoji setting, is a bitmap font: 109 px
  pictures that fontconfig scales to the text's size by the rules of its
  `10-scale-bitmap-fonts.conf`. A system `fonts.conf` includes them from
  `conf.d`; the one the framework writes where there is none - every packaged
  Windows build, and Android - did not, so an emoji stood 128 px high in a
  line of 14 pt text (UltraMail's message list and reading pane showed a
  LinkedIn subject's 👤 as a giant silhouette). The generated `fonts.conf`
  now carries the scaling rules on Windows and Android, Windows' inline
  configuration for an empty fontconfig loads them too, and
  `GenerateBitmapScalingConfig()` (`UltraCanvasApplication.h`) hands them to
  any other configuration. Linux keeps taking them from `/etc/fonts/conf.d`.
  `EmojiSettingsTest` checks that a 14 pt line with the colour emoji stays
  under 40 px.
