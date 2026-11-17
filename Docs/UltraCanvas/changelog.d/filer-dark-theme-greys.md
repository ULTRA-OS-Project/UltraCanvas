- **The file display's last fixed greys follow the theme.** Under a dark
  theme four of them stayed in their light-look shade and were close to
  unreadable on the dark well or dialog:
  - the scroll bars' track and thumb: `FilerStyle::scrollbarTrackColor`,
    `scrollbarThumbColor` and `scrollbarThumbHeldColor`, which `FromTheme`
    fills with a light veil for the track under a dark theme, the muted ink
    for the thumb and the ink while it is dragged;
  - the gear beside the hidden-items notice: `FilerStyle::toolGlyphColor`,
    `toolHoverColor` and `toolPressedColor`, the muted ink on the theme's
    hover and pressed fills, applied again on every `SetStyle`;
  - the caption over the list in the copy / move / conflict dialogs and the
    note under it, now `LabelVariant::Muted` labels rather than fixed
    colours.

  The defaults of a `FilerStyle` built by hand are the old colours, so a
  host that does not follow the theme looks as before.
