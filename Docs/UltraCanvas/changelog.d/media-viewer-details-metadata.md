- **Media viewer Details panel: image metadata, scrollable, laid out as
  Markdown.** `UltraCanvasMediaViewer::UpdateDetailedInfo` listed only the
  header facts (size, dimensions, channels, colour space, dpi, loader) and
  never read the file's own metadata, so EXIF, IPTC, XMP, ICC and PNG text
  chunks were invisible in UltraFiler and UltraViewer. It now adds a
  "Metadata" section from `PixelFX::Header::ReadMetadata`, one sub-section per
  block, or a line saying the file carries none. The details were drawn by
  `UltraCanvasMediaSurface` as plain text in a box fixed at 280 px, which the
  header facts alone already filled, and only over images. They now show in
  an `UltraCanvasTextArea` (`MarkdownHybrid`, dark theme) placed over the
  active view for every media kind: a heading per section over a two-column
  Property / Value table, scrolled by the wheel, Up / Down and PageUp /
  PageDown, closed with Escape. New `SetDetailsVisible()` /
  `ToggleDetails()` / `IsDetailsVisible()`; the surface's `SetInfoText()` /
  `ToggleInfoPopup()` / `IsInfoPopupVisible()` are gone (nothing outside the
  viewer used them). Docs: `UltraCanvasMediaViewer.md` §Details panel.
- **Markdown tables in `UltraCanvasTextArea` keep short columns readable.**
  When a table was wider than the view, every column shrank in proportion,
  so one long cell (a path, a URL) squeezed a column of short labels until
  they broke mid-word ("Dimens-ions"). `NormalizeTableGroupWidths` now lets a
  column that fits its fair share keep its natural width and shrinks only
  the wider ones.
- **PixelFX decodes IPTC and XMP into one row per tag**
  (`PixelFX/PixelFXMetadataDecode.h`, `Header::DecodeIPTC` /
  `Header::DecodeXMP`). libvips lists EXIF tag by tag but hands IPTC and XMP
  over as raw blocks, so `Header::ReadMetadata` - and with it the Details
  panel and `UltraCanvasMetadataDialog` - showed only "iptc-data: 56 bytes"
  and "xmp-data: 2988 bytes". IPTC is read from bare IIM (TIFF) and from the
  Photoshop APP13 "8BIM" wrapper a JPEG carries: record-2 datasets get their
  IIM names (Keywords, By-line, City, Caption/Abstract, ...), repeated ones
  are joined, dates and times are written 2026-09-20 / 14:32:11+01:00, and
  Latin-1 text becomes UTF-8 unless the block declares UTF-8. XMP is read
  with tinyxml2: every property of every `rdf:Description` as
  `prefix:Name`, written as an attribute or an element - language
  alternatives (x-default first), bags and sequences, structures
  (`prefix:Struct/prefix:Field`, arrays of them indexed), resources. A block
  that does not decode keeps its size row. The raw `exif-data` row is
  dropped once libvips has listed the EXIF tags, since it repeated them as
  "data: 518 bytes". Test: `Tests/PixelFXMetadataDecodeTest.cpp` (bare and
  wrapped IIM, encodings, truncated and lying blocks, every XMP form,
  malformed XML, value length cap).
- **PixelFX writes EXIF values the way a camera app shows them**
  (`Header::HumanizeExif` / `Header::SplitExifString` in
  `PixelFX/PixelFXMetadataDecode.h`). `Header::ReadMetadata` showed libvips'
  raw strings, only trimmed: `28/5 (f/5.6)`, `51/1 30/1 0/1 (51)`, `0/1 ( 0)`,
  `ResolutionUnit: 1`. Now: `f/5.6`, `1/250 s`, `50 mm`, `ISO 400`,
  `-0.67 EV`, aperture and shutter speed converted from their APEX values,
  `LensSpecification: 24–70 mm f/2.8`, dates as `2026-09-20 14:32:11`,
  `Orientation: Rotated 90° clockwise` and the meaning of every coded number
  (`MeteringMode: Pattern`); a code libexif does not know stays a number. GPS
  reads `51° 30′ 0″ N (51.5°)`, `35 m` (or `below sea level`), `13:32:11
  UTC`, `123.4° (true north)`, speed in km/h, mph or knots. `…Ref` and unit
  fields fold into the value they qualify (`XResolution: 300 dpi`); unset
  values (a `0/1` resolution, a digital zoom of 0), strip and thumbnail
  offsets are left out; the embedded thumbnail's fields are named
  `Thumbnail …` instead of colliding with the image's. The EXIF tags are
  formatted together because a value can depend on another field.
  `TrimExifAnnotation` is gone; `SplitExifString` replaces it. Tests:
  `Tests/PixelFXMetadataDecodeTest.cpp` gains the strings libvips 8.15
  produces for a Canon JPEG, GPS above and below sea level, decimal minutes,
  and malformed rationals.
- **Metadata tags carry names a person reads** (`Header::FriendlyTagName`
  in `PixelFX/PixelFXMetadataDecode.h`, applied by `Header::ReadMetadata`).
  The Details panel and `UltraCanvasMetadataDialog` listed technical keys:
  `DateTimeOriginal`, `FNumber`, `GPSLatitude`, `By-line`,
  `Caption/Abstract`, `dc:subject`,
  `Iptc4xmpCore:CreatorContactInfo/Iptc4xmpCore:CiEmailWork`,
  `jpeg-chroma-subsample`. They now read `Date taken`, `F-number`,
  `Latitude`, `Author`, `Caption`, `Keywords`, `Creator contact › Email`,
  `Chroma subsampling`, from tables of the common EXIF, IPTC and XMP tags; a
  tag without a name of its own is split into sentence-case words with
  acronyms kept (`SensingMethod` → `Sensing method`) and its XMP prefix
  dropped, IIM's title-case names become sentence case, and a PNG text chunk
  shows its own keyword. The Details panel also stops escaping parentheses,
  which showed as `Time zone \(taken\)` because `UltraCanvasTextArea` does
  not unescape inside bold. Tests: `Tests/PixelFXMetadataDecodeTest.cpp`
  gains the names, including the word splitting.
- **The last raw metadata values are tidied.** XMP dates read
  `2026-09-20 14:32:11 +01:00` (a `Z` as `UTC`), `True` / `False` read Yes /
  No, and a rating reads `4 of 5` (`Not rated`, `Rejected`). IPTC, XMP and
  the Photoshop "8BIM" block that ImageMagick stores in a PNG as a "Raw
  profile type" text chunk of hex digits are decoded
  (`Header::DecodeRawProfile`) instead of shown as hex. libvips' own fields
  read as values (`Header::TidyOtherValue`): Progressive / Interlaced Yes /
  No, Loop count `Forever` / `Once` / `3 times`, Frame delays `100 ms per
  frame`, a GIF palette `16 colours`, the background `RGB 255, 255, 255`;
  `resolution-unit` is left out, and `orientation` when EXIF has it. A
  resolution of 25.4 dpi - libvips' stand-in when the file stores none - is
  left out of the Image group and the Details panel, and the panel writes
  `300 dpi` instead of `300 x 300 dpi` and rounds `95.9866` to `95.99`.
  Tests: `Tests/PixelFXMetadataDecodeTest.cpp` gains XMP values, raw
  profiles and libvips' fields.
