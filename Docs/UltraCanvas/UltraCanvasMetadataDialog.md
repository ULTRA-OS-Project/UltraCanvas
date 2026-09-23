# Showing a file's metadata

Two pieces: a reader that turns a file's metadata into text, and a popup that
shows it.

## Reading it

`PixelFX::Header` (`PixelFX/PixelFX.h`) reads the metadata of anything libvips
decoded:

```cpp
PixelFX::PFXImage image = PixelFX::FileIO::Load(path);

if (PixelFX::Header::HasMetadata(image)) {                       // anything to show?
    std::string listing = PixelFX::Header::MetadataToText(image); // Markdown by default
}
```

| Call | |
|---|---|
| `HasMetadata(image)` | True when the **file** carried metadata — the geometry libvips reports for every image (width, bands, interpretation, …) does not count. This is the check a UI makes before offering a "Show metadata" button. |
| `ReadMetadata(image)` | `std::vector<MetadataEntry>` — `{group, key, value}`, grouped `Image`, `EXIF`, `IPTC`, `XMP`, `Colour`, `Other`. |
| `MetadataToText(entries \| image, format)` | The entries as one string: `MetadataTextFormat::Markdown` (a `## Group` heading and a two-column table per group) or `MetadataTextFormat::PlainText` (aligned `Tag : value` lines under an underlined heading). |
| `GetFields(image)` | The raw libvips field names, if you want to do your own thing. |

The reader does the tidying a person would otherwise have to do in their head:

- The **Image** group is written the way a person says it — `Dimensions:
  640 x 480 px`, `Colour space: sRGB`, `Resolution: 72 dpi`, `Read by:
  jpegload` — instead of the dozen raw fields underneath it.
- Tags carry names a person reads (`Header::FriendlyTagName`): `Date taken`,
  `F-number`, `Exposure compensation`, `Camera model`, `Latitude` for EXIF;
  `Author`, `Caption`, `Country` for IPTC; `Title`, `Keywords`, `Created with`,
  `Creator contact › Email` for XMP; `Chroma subsampling`, `ICC profile` for
  the rest. A tag without a name of its own is split into words
  (`SensingMethod` → `Sensing method`), XMP namespace prefixes dropped.
- EXIF values are written the
  way a camera app shows them (`Header::HumanizeExif`,
  `PixelFX/PixelFXMetadataDecode.h`) instead of libvips' raw string
  (`28/5 (f/5.6, Rational, 1 components, 8 bytes)`):
  `FNumber: f/5.6`, `ExposureTime: 1/250 s`, `ApertureValue` and
  `ShutterSpeedValue` converted from APEX, `FocalLength: 50 mm`,
  `ISOSpeedRatings: ISO 400`, `ExposureBiasValue: -0.67 EV`,
  `LensSpecification: 24–70 mm f/2.8`, `DateTimeOriginal: 2026-09-20
  14:32:11`, `Orientation: Rotated 90° clockwise`, and the meaning of every
  coded number (`MeteringMode: Pattern`, `ColorSpace: Uncalibrated`). GPS
  comes as `51° 30′ 0″ N (51.5°)`, `35 m`, `13:32:11 UTC`, `123.4° (true
  north)`. A `…Ref` or unit field is folded into the value it qualifies
  (`XResolution: 300 dpi`), unset values (a `0/1` resolution, a zoom ratio of
  0) and file offsets are left out, and the embedded thumbnail's fields are
  named `Thumbnail …`.
- The IPTC and XMP blocks, which libvips keeps as raw bytes, are decoded into
  one row per tag (`PixelFX/PixelFXMetadataDecode.h`): IPTC by its IIM names
  (`Keywords`, `By-line`, `City`, `Caption/Abstract`, dates as `2026-09-20`),
  from bare IIM or a JPEG's Photoshop "8BIM" block, Latin-1 converted to UTF-8;
  XMP as `prefix:Name` for every property (`dc:title` in its x-default
  language, `dc:subject` bags joined, structures as `prefix:Struct/prefix:Field`).
  A block that does not decode is still listed by size.
- Other binary blocks (ICC profiles) are reported by size, not dumped; the raw
  EXIF block is left out once its tags are listed.
- Markdown special characters are escaped, so `VIPS_CODING_NONE` does not come
  out italicised with its underscores eaten.

## Showing it

`UltraCanvasMetadataDialog` (`dialogs/UltraCanvasMetadataDialog.h`) is a
read-only popup around an `UltraCanvasTextArea`. In Markdown mode the text area
renders the headings and lays the tables out, and — being a real text area — it
scrolls, selects and copies without the dialog doing any of it.

```cpp
ShowMetadataDialog(fileName, listing, /*markdown*/ true, parentWindow);
```

It takes **text**, not fields, so anything that can describe itself can use it:
a document reader, a font viewer or an audio tag reader produces the same two
formats and gets the same popup. `Copy` puts the whole listing on the clipboard.

## Who uses it

The image export dialog (*Export with Options* in UltraPaint, the DemoApp's
export button): its Metadata section shows a **Show…** button and an entry
count only when the image being saved actually carries metadata — an image
flattened in memory carries none, and then there is nothing to offer.

## See also

- [PixelFX](../Modules/PixelFX/README.md) — the libvips-backed image pipeline
- [UltraCanvasTextArea](UltraCanvasTextAreaExamples.md) — the Markdown view the popup uses
