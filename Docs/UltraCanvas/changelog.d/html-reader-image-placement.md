- **HTML reader: images sit where a browser puts them, and linked images are
  shown.**
  - An `<img>` in a block (`<p><img></p>`) was drawn in the middle of the
    line. Block flow gave its element the full column width, and the image
    element draws its bitmap centred in its box. Each image now sits on a
    full-width line of its own at its natural size (still capped at the column
    width, keeping its aspect ratio). It is placed at the start of the line,
    or centred or right-aligned by the `text-align` it inherits.
  - That `text-align` now also comes from the presentational
    `align="left|center|right|justify"` attribute on `p`, `div`, `td`, `th`,
    `h1`–`h6`, `caption` and `img`, and from `<center>`, which is now a block
    element. All of these are still common in email HTML. CSS `text-align`
    still wins over the attribute.
  - An image inside an inline element was replaced by its `[alt]` text. This
    covers `<a href><img></a>`, the banner and button of nearly every
    newsletter, and `<span><img></span>`. Such images are now lifted onto
    lines of their own, and one inside a link is clickable (it calls
    `BuildOptions::onLinkActivated` with the link's href). The text around
    them stays in its runs.
  - `ElementBuilder::BuildImage` takes an optional link href.
  - Test: `Tests/HTMLImageAlignTest.cpp` (headless builder + CSSLayout;
    placement for each alignment source, an oversized image, a clicked link).
