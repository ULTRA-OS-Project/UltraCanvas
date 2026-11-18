- **PowerPoint text inside `mc:AlternateContent` is read.** PowerPoint wraps
  what an older reader may not know - equations, newer charts, 3D models,
  ink - in Markup Compatibility's `mc:AlternateContent`, and the `.pptx`
  reader skipped the whole element, so an equation's text box vanished from
  the outline. It now reads the first `mc:Choice` that gives a shape (or,
  inside a paragraph, text), else the `mc:Fallback` - never both. An equation
  (`a14:m`) reads as its text, a chart it cannot read as its fallback
  picture. `OfficeEncryptionTest` rebuilds the PowerPoint fixture with the
  three cases.
- **`FilePreviewsTest`: "the text is drawn" no longer depends on the font.**
  The word-processing first-page check counted pixels darker than a fixed
  dark grey. At 96 px the fixture's Liberation Serif drawn unhinted (a cloud
  session has Liberation, installed with LibreOffice, and an image-wide
  fontconfig rule that turns hinting off) gave 10 and 17 such pixels against
  the 20 asked, while CI, without Liberation, draws DejaVu and gave 26-36. It
  counts neutral grey darker than mid-grey now (80-290 across those setups;
  a blank page has none), and leaves the page's red box and highlight out.
