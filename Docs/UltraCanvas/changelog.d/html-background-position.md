- **HTML backgrounds honour `background-position`.** A background picture was always
  drawn centred; it now sits where CSS puts it.
  - `background-position` and the position inside the `background` shorthand:
    keywords (`left` / `center` / `right`, `top` / `center` / `bottom`, in either order),
    percentages, lengths and the edge-offset form (`right 10px bottom 20%`). The default
    is CSS's `0% 0%`, the top-left corner - mail that wants a picture centred says
    `center`, as it must for a browser.
  - Size and position are kept per background layer, and a shorter
    `background-size` / `background-position` list repeats across the layers, as in
    CSS; the layer that loads uses its own values.
  - `UltraCanvasImageElement::SetImagePosition` (`ImagePosition` /
    `ImageAxisPosition`): where a fitted image sits in its element - a fraction of the
    free space or a pixel offset from either edge - and `ImageDrawRect()`, the
    rectangle it is drawn into. Centred (the default) draws exactly as before.
  - Fixed: an image loaded from memory (every picture in a mail) and drawn unscaled
    (`ImageFitMode::NoScale`) was never shown - the Cairo backend read its pixels from
    a file name it does not have. It now reads the bytes it was loaded from, as the
    other fit modes already did.
