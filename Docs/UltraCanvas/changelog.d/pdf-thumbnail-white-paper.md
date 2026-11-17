- **A PDF's thumbnail is a white page in the dark look too.** The PDF engine
  hands a page back with the paper it does not paint transparent - nearly
  every PDF leaves its paper unpainted - so on a dark tile (UltraFiler,
  UltraDesktop's boards, UltraToDo's attachments) a PDF showed as a dark page
  with only its own white boxes light. The first page is now laid on white
  paper: what the page leaves transparent is white, what it paints keeps its
  colour, and the thumbnail is opaque. The thumbnail disk cache's renderer
  generation is 4, so dark thumbnails already cached are drawn again once.
  `FilePreviewsTest` checks it on a new fixture, `pdf-blue-square.pdf`
  (`Tests/fixtures/make-pdf-fixtures.py` writes it).
