- **CSSLayout: a flex item is measured at the cross extent it is given.** A
  column measured each child's height at the column's whole width, but then
  arranged a stretched child narrower by its own side margins. A wrapping
  label that just fitted on one line at the full width needed two at the
  narrower one, got one line's height and lost its second line - the Group
  Box page in German. The item's cross margins now come off the extent it is
  measured at, for its base size and for its line's cross size (a row's
  items likewise against their top and bottom margins).
  `Tests/CSSLayoutCrossMarginTest` holds it.
