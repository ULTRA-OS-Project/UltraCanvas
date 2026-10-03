- **HTML backgrounds honour `background-position` and `background-repeat`.** A
  background picture was always drawn once, centred; it now sits and tiles where CSS
  puts it.
  - `background-position` and the position inside the `background` shorthand:
    keywords (`left` / `center` / `right`, `top` / `center` / `bottom`, in either order),
    percentages, lengths and the edge-offset form (`right 10px bottom 20%`). The default
    is CSS's `0% 0%`, the top-left corner - mail that wants a picture centred says
    `center`, as it must for a browser.
  - `background-repeat` (and the repeat inside the shorthand): `repeat`, `repeat-x`,
    `repeat-y`, `no-repeat`, one value or one per axis; `space` and `round` are taken as
    `repeat`. The default is CSS's `repeat`, so a background without `no-repeat` now
    tiles, as in a browser - the 1-pixel gradient strip behind a mail's header fills it.
  - Size, position and repeat are kept per background layer, and a shorter
    `background-size` / `background-position` / `background-repeat` list repeats
    across the layers, as in CSS; the layer that loads uses its own values.
  - `UltraCanvasImageElement::SetImagePosition` (`ImagePosition` /
    `ImageAxisPosition`): where a fitted image sits in its element - a fraction of the
    free space or a pixel offset from either edge - and `ImageDrawRect()`, the
    rectangle it is drawn into. Centred (the default) draws exactly as before.
  - `UltraCanvasImageElement::SetImageRepeat(x, y)`: the image tiles across and / or down
    the element, lined up on its positioned tile, drawn as one pattern fill
    (`CreatePixmapPattern`, `PatternExtend::Repeat`) - tile by tile only where a backend
    has no patterns.
  - Fixed: an image loaded from memory (every picture in a mail) and drawn unscaled
    (`ImageFitMode::NoScale`) was never shown - the Cairo backend read its pixels from
    a file name it does not have. It now reads the bytes it was loaded from, as the
    other fit modes already did.
