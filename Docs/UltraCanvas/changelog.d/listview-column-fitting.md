- **List view: every column fitted to its text.** `SetColumnFitting` makes
  each column as wide as its widest `DisplayRole` text plus the delegate's
  padding, never narrower than its header, within a minimum and a cap -
  measured at the first paint after the rows change, and no longer once the
  user drags a border. UltraMail's Date column did this with a subclass;
  UltraDBAdmin's grids estimated widths from a character count, so a column
  of narrow glyphs was wider than it needed to be and a wide one still cut
  its text.
- **Container: the corner between two scrollbars is painted.** With both
  scrollbars showing, the square where their tracks meet was left to
  whatever lay beneath; it is the track's colour now.
