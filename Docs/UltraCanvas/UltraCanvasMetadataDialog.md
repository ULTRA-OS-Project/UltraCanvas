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
- EXIF tag names lose their `exif-ifd0-` prefix, and values lose the encoding
  libvips appends (`UltraCanvas Cameras (UltraCanvas Cameras, ASCII, 20
  components, 20 bytes)` → `UltraCanvas Cameras`). Where libvips' own reading of
  a numeric tag says more than the number, it is kept: `65535 (Uncalibrated)`,
  `2 (Inch)`.
- Binary blocks (ICC profiles, the raw EXIF/XMP/IPTC payloads) are reported by
  size, not dumped.
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
