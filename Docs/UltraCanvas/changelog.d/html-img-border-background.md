- **HTML `<img>` draws its border, background, padding and rounded corners.** An
  image's CSS box was lost: a block image squeezed its border and padding into the
  picture's `width` / `height`, and an image in running text drew none of it.
  - `width` / `height` size the picture itself (CSS's `content-box`); border and
    padding go around it, and horizontal margins stay margins, so the background
    does not fill them - the 8px gap after an icon in a mail's button is back.
  - An image in running text gets its frame: `LabelInlineImageFrame` (margins,
    padding, border, `borderRadius`, `background`) on `LabelInlineImage::frame`.
    The line reserves the whole margin box; `InlineImageBoxRect(i)` is the border
    box, `InlineImageRect(i)` still the picture.
  - `border-radius` clips the picture too, block and inline, and a percentage is
    kept (`ComputedStyle::borderRadiusPercent`) and resolved against the image's
    box: `border-radius: 50%` makes a round avatar.
  - `<img border="N">`: an N-pixel border in the image's colour - the link colour
    for a linked image, as in a browser.
  - A `border` shorthand without a colour uses the text colour (CSS's
    `currentColor`) instead of black.
  - CSSLayout: a flex item with `box-sizing: content-box` and an explicit main
    size now gets a flex base size that includes its padding and border, as the
    block path already did. (Widgets default to `border-box` and are unaffected.)
