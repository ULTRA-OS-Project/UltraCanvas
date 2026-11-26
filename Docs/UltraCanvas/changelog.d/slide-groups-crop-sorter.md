- **Slides: groups, alignment, picture crops, the slide sorter, notes as
  paragraphs and a chart's data, in the deck model and the slide editor.**
  - The deck model keeps group shapes: `PptxShape::groupPath` (the groups a
    shape is in, outermost first) and `PptxSlide::groupNames`. The .pptx
    reader keeps `p:grpSp` (its `chOff` / `chExt` scaling applied as before,
    a turned group's shapes now turned with it) and the writer writes it
    back, nested, its box the shapes'; the .odp reader and writer keep
    `draw:g`. `PptxGroupIds`, `PptxNewGroupId`.
  - A picture's crop is read from and written to an .odp's `fo:clip`, in
    lengths of the picture at its own size (`PptxPictureNaturalSize`: its
    pixels at the resolution a PNG's `pHYs` or a JPEG's JFIF states, else
    96 to the inch, as LibreOffice measures it); the .pptx's `a:srcRect` was
    kept already.
  - `UCSlideDeckEditor`: groups are selected whole (`Select`,
    `ToggleSelected`, `ShapesInside`), `GroupSelection` / `UngroupSelection`
    (one step each), `EnterGroup` / `ExitGroup` to choose inside one,
    `RotateSelection` turns a group as one, the z-order commands keep a
    group together and a copy of a group is a group of its own;
    `AlignSelection(SlideAlignment)` and `DistributeSelection` (to the
    slide for one shape); `SetPictureCropBox`, `SetPictureCrop`,
    `ResetPictureCrop`, `PictureFullBox`; `SetShapeChart`, `ChartDataRows`,
    `ChartWithDataRows` (a chart's data as cells, its kind kept);
    `SetSlideNotes(index, paragraphs)`; `MoveSlides`, `DuplicateSlides`,
    `DeleteSlides`, `SetSlidesHidden` for several slides as one step.
  - `UltraCanvasSlideEdit`: Ctrl+G / Ctrl+Shift+G, a group framed, resized
    and turned with its own handles, a double-click selecting inside it and
    Escape going back; crop mode (`SetCropMode`) with crop handles over the
    picture's faint whole; the slide sorter (`SetView(SlideEditView::Sorter)`:
    a grid of thumbnails, click, Ctrl and Shift choose, a drag moves them
    together, Ctrl+D, Delete, a right-click for a host's menu);
    `onEditChart` on a double-click on a chart, `onModeChange`.
  - The .pptx reader no longer bullets the speaker notes by default (the
    slide master's body style was applied to them); the .odp writer no longer
    reads past the theme's colours for a table in a deck made in memory.
  - `PptxSlideDrawOptions::background`: false draws a slide's shapes alone,
    over what the context holds.
