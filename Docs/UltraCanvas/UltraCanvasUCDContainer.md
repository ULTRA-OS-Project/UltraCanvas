# UltraCanvas UCD Container

<!-- doc-check: std::string path; std::vector<uint8_t> png; std::vector<uint8_t> payload; -->

`UltraCanvasUCDContainer.h` reads and writes the **UltraCanvas Document
container, version 2** (`*.ucd`): the layer every UCD content type is built
on. The format itself is specified in
[`UCD-FileFormat-v2.md`](UCD-FileFormat-v2.md); this page is the API.

| Part | What it is |
|---|---|
| `UCD::Writer` | Builds a file: a type descriptor, an optional PNG thumbnail, sections; writes an `INDX` first and stages the write |
| `UCD::Reader` | Reads a whole file: header, thumbnail, every section as stored; decodes a section on request |
| `UCD::ReadHeaderAndThumbnail` | A filer's read: the 28-byte header and the raw thumbnail, the body untouched |
| `UCD::AppendPreamble` / `ReadPreamble` | The 8-byte sub-format preamble a `VECT` / `BMAP` / `VIDS` / `AUDS` / `SC3D` payload starts with |
| `UCD::ByteWriter` / `ByteReader` | Little-endian, bounds-checked bytes - what section payloads are built from |

It does not interpret sections. A layered image's `LAYR` / `BMAP` / `VECT`
belong to `UCRasterDocument` (`SaveToFile` / `LoadFromFile` on a `*.ucd`, see
[`UltraCanvasPaintSurface.md`](UltraCanvasPaintSurface.md), *Layered files*);
the container hands each content type payload bytes.

## Writing

```cpp
#include "UltraCanvasUCDContainer.h"
using namespace UltraCanvas;

UCD::Writer writer("UCBitmap");          // the descriptor: at most 8 ASCII characters
writer.SetThumbnailPng(png);             // raw, straight after the header
std::vector<uint8_t> bitmap;
UCD::AppendPreamble(bitmap, "UCRW", 1);  // what the payload's encoding is
UCD::ByteWriter w(bitmap);
w.U16(2); w.U16(2); w.U8(0); w.Zeros(3); // UCRW: width, height, RGBA8, reserved
w.Zeros(2 * 2 * 4);                      // the pixels
writer.AddSection("BMAP", bitmap);       // deflated (a zlib stream) by default
writer.AddSection("MEDI", payload, UCD::Compression::Uncompressed);   // already compressed
std::string error;
if (!writer.WriteToFile(path, error)) { /* the file that was there is untouched */ }
```

- The header is version **2.1**, binary body, deflate by default, key source
  none; the thumbnail flags say PNG.
- Sections are written in the order added, after an `INDX` that lists each
  one's type, offset, stored and whole length.
- `WriteToFile` writes through `WriteFileAtomically`: a failure leaves the
  previous file as it was. `WriteToBuffer` gives the bytes.
- Only *Uncompressed* and *Deflate* are written; there is no encryption yet.

## Reading

```cpp
#include "UltraCanvasUCDContainer.h"
using namespace UltraCanvas;

UCD::Reader reader;
std::string error;
if (reader.Open(path, error)) {
    const UCD::Header& h = reader.GetHeader();      // descriptor, version, flags ...
    for (const UCD::Section* s : reader.FindSections("BMAP")) {   // file order
        std::vector<uint8_t> bytes;
        if (!reader.Decode(*s, bytes, error)) continue;           // damaged, encrypted ...
        std::string fourcc;
        uint8_t version = 0;
        if (UCD::ReadPreamble(bytes, fourcc, version) && fourcc == "UCRW") {
            UCD::ByteReader r(bytes.data() + UCD::kPreambleSize, bytes.size() - UCD::kPreambleSize);
            uint16_t width = 0, height = 0;
            r.U16(width); r.U16(height);
            if (!r.Ok()) continue;                  // a read past the end fails
        }
    }
    (void)h;
}
```

- `Open` reads the whole file and lists every section, unknown types
  included - the caller skips what it does not know (the spec's
  forward-compatibility rule).
- `Decode` gives the payload as serialized: it inflates zlib and gzip
  sections (checksums checked), resolves the header-default code `0xFF`, and
  refuses - with the reason - an encrypted section, LZMA, a damaged stream,
  or a section that would decompress past `limit` (2 GiB by default).
- Refused outright, with a message: a damaged signature (all 12 bytes are
  checked), a version 1 file (`UCD\x01`), a later **major** version, a file
  cut short. A later **minor** version reads.

## The filer's read

```cpp
#include "UltraCanvasUCDContainer.h"
using namespace UltraCanvas;

UCD::Header header;
std::vector<uint8_t> thumbnail;
std::string error;
if (UCD::ReadHeaderAndThumbnail(path, header, thumbnail, error) && header.HasThumbnail()) {
    // thumbnail holds a PNG (header.ThumbnailIsPng()) or HEIC file
}
```

It reads 28 bytes and the thumbnail - nothing of the body. This is what
`ExtractEmbeddedPreviewBytes` returns for a `.ucd` (UltraFiler's thumbnails)
and what the media viewer shows; both leave a `PRIVATE` document's preview
out.

## Tests

`Tests/UCDContainerTest.cpp` (`UCDContainerTest`): the header and `INDX`
bytes a writer puts down, the round trip through a buffer and a file, the
header-default code, gzip, an unknown section, LZMA and encrypted sections
refused, damaged checksums, the refusals above, the preamble and the byte
helpers. `RasterEditingTest` covers layered images on top of it, and
`EmbeddedPreviewTest` the thumbnail as an embedded preview.
