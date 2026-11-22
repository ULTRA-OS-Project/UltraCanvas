- **A menu's border no longer breaks off at its rounded corners.** A popup
  in a window of its own is cut to an outline of its opaque pixels, and only
  pixels at least half opaque were kept and copied as they were: the outer,
  fainter half of the 1 px border round each corner was cut away and the
  rest came out washed white on Windows (dark on X11), so the corners looked
  open and square against a light window behind. The outline now keeps
  pixels from a quarter opaque, and the popup's window shows every pixel it
  keeps at full strength in its own colour, so the border runs round the
  corner unbroken.
- **A menu's title stands apart.** A header row (`MenuItemData::Header`)
  sits on a band darker than the panel - the new
  `MenuStyle::headerBackgroundColor`, reaching up to the border when the
  title is the first row - and its text is dark grey (the theme's ink a
  shade lighter with `FromTheme`) instead of the light grey that read as a
  disabled entry.
