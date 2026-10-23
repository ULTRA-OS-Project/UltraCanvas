- **`UltraCanvasSegmentedControl` tints line icons and centres an icon with
  its caption.** `SegmentedControlStyle::iconAsMask` draws each segment's
  icon as a mask in that segment's text colour, so a one-colour icon turns
  white on the selected segment with its caption instead of staying dark on
  the blue. An icon beside a centred caption is centred together with it;
  it used to sit at the segment's left edge with the text centred in what
  was left. ArtCreator's Scale / Rotate switch is the first user.
