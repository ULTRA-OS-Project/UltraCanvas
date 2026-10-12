- **HTML reader: a plain inline element's box - margin, padding, border and
  background - is drawn.** `<nav><a style="margin-right:18px">About</a><a>Index</a></nav>`
  showed "AboutIndex", and the mail button
  `<a style="background:#07f;padding:12px 24px;color:#fff">Buy</a>` was white
  text on white: inline elements went into the label's Pango markup with no
  box, so their margins were dropped and their backgrounds not painted.
  - **Margin, border and padding** on the left and right are room in the
    line: a picture-less U+FFFC placeholder of that width in the run. A word
    joiner (U+2060) glues it to the element's first or last letter, so a line
    does not break between a word and its margin. The margin lies outside the
    element's link and the padding inside it; white space collapses across
    the room; negative margins take none; vertical margins move nothing, as
    in CSS; an empty element with a margin builds no line.
  - **Background and border:** drawn by the label around the element's text
    and padding, at any depth in a run (`<b><span style="border:...">`). The
    new `LabelTextBox` (`UltraCanvasLabel::SetTextBoxes`) is a range of the
    text with a background, four border sides, vertical padding and a
    radius. It is drawn a piece per line, so a long bordered element breaks
    across lines like in a browser: the left border on its first piece, the
    right on its last. Its vertical padding is painted without moving the
    lines.
  - **Vertical padding with a background or a border** (the mail button):
    the element is a box on the line, as an `inline-block` is, with its
    padding painted all round and its line grown to hold it.
  - A space between an inline box and the text beside it is kept ("Order
    now: [Buy] today" read "Order now:[Buy]today"). This applies to
    `inline-block` buttons too.
  - `UltraCanvasLabel` leaves word joiners out of copied text.
  - New `HTMLInlineBoxTest`: room, line breaks, links, and painted pixels for
    backgrounds, the button, borders (nested and across lines) and radii.
