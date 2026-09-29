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
  - **An image in the middle of a sentence flows in the text** instead of
    taking a line of its own. Images in a block that also has text (`<p>Rated
    <img> out of five</p>`) become inline images of the text run. A block of
    images alone keeps one image per aligned line. The image stands on the
    baseline, the line grows to hold it, it is scaled to the line when wider,
    and inside a link it is part of the link.
  - Inline images honour vertical alignment: CSS `vertical-align`
    (`baseline`, `middle`, `top`/`text-top`, `bottom`/`text-bottom`) and the
    `<img align>` values `middle`/`absmiddle`, `top`/`texttop` and
    `bottom`/`absbottom`.
  - New in `UltraCanvasLabel`: `LabelInlineImage` (with
    `LabelInlineImageAlign`), `SetInlineImages()` and `InlineImageRect()`. An image is drawn at a U+FFFC placeholder in the
    text, in a box reserved with `TextAttributeFactory::CreateShape`. See
    `UltraCanvasLabelExamples.md`, *Inline Images*.
  - `ElementBuilder::BuildImage` takes an optional link href.
  - Test: `Tests/HTMLImageAlignTest.cpp` (headless builder + CSSLayout;
    placement for each alignment source, an oversized image, a clicked link,
    an inline image laid out and drawn on an offscreen context, and each
    vertical alignment measured against a baseline image in the same line).
