- **HTML `<img>` honours `object-fit` and `object-position`.** A picture was always
  shrunk to fit its box, keeping its shape, and centred; it now fills and sits in the
  box as CSS says - for a block image and for one inside running text.
  - `object-fit`: `fill`, `contain`, `cover`, `none`, `scale-down`. The default is
    CSS's `fill`: an `<img>` whose `width` / `height` give it another shape than the
    picture's stretches the picture to the box, as in a browser, instead of leaving
    empty bands beside it.
  - `object-position`: the `background-position` value forms (keywords, percentages,
    lengths, edge offsets); the default is CSS's `50% 50%`, centred.
  - `ImagePosition`, `ImageAxisPosition` and the new `FitImageRect(natural, box, fit,
    position)` moved to `UltraCanvasCommonTypes.h`, so any element can place a fitted
    picture the same way; `UltraCanvasImageElement.h` still brings them in.
  - `LabelInlineImage` gained `fit` (default `ImageFitMode::Fill`, what it drew before)
    and `position` (default centred).
