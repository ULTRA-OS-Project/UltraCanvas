- **`UltraCanvasFilerWidget::SetCaptionMaxLines` / `GetCaptionMaxLines`.** A
  host can change how many lines a thumbnail tile's or treemap cell's name may
  wrap over without replacing the whole `FilerStyle`; the grid is relaid out
  with the new limit. `FilerStyle::captionMaxLines = 0` now means *no limit* -
  the full name is shown over as many lines as it needs - where it used to be
  read as 1. UltraFiler's *Settings > Display > File names* drives it.
