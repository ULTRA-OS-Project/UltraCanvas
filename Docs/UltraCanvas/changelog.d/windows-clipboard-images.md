- **Windows: pictures cross the clipboard between UltraCanvas applications
  and every other program.** Nothing image-shaped went either way before:
  - *Copy* put the application's PNG bytes under `CF_DIB`, the format that
    must hold a bitmap header and pixels. Paint Shop Pro, Paint, Word and
    the rest read a header that was not there and pasted nothing. An image
    now goes on as `"PNG"` (browsers, Office, GIMP, Paint.NET and Krita read
    it, transparency included), `CF_DIBV5` (32-bit with straight alpha) and
    `CF_DIB` (24-bit, transparency flattened onto white, for the programs
    that read only a bitmap; Windows makes `CF_BITMAP` from it).
  - *Paste* handed back the raw `CF_DIB` block as `image/bmp` - a bitmap
    with no file header, which no decoder reads - so a picture copied in
    another program pasted as nothing in UltraPaint, and UltraFiler's Paste
    wrote it to a `Pasted image.bmp` nothing could open. `GetImage` now
    returns PNG on Windows as on the other desktops: the `"PNG"` format when
    the source offered one (trimmed to its `IEND`; a clipboard block is often
    larger than its content), else `CF_DIBV5` or `CF_DIB` converted. 1/4/8-bit
    palettes, 16/24/32-bit `BI_RGB` and bit fields, `BI_PNG`, and rows stored
    either way up are read; alpha is taken only from a bit-field DIB, and a
    picture whose alpha is zero everywhere reads as opaque.
  - The conversion is `UltraCanvasClipboardDib.h` (`ClipboardDib::DecodeDib`,
    `EncodeDibV5`, `EncodeDib24`, `DibToPng`, PNG through cairo): plain byte
    work, so `Tests/ClipboardDibTest.cpp` checks it on Linux.
- **Windows: a copied file list goes on the clipboard the way Explorer puts
  it.** Next to `CF_HDROP` it now carries `"Shell IDList Array"` (what
  programs built on the shell's data-object helpers read), `"FileNameW"` and
  `"Preferred DropEffect"`, all written in one open of the clipboard. The cut
  marker used to be added in a second open, after another program could
  already have read the list as a copy; a plain `SetFiles` now marks a copy,
  as Explorer does.
- **Windows: a clipboard another program holds for a moment no longer makes
  a copy or paste silently do nothing.** A clipboard manager or Remote
  Desktop's `rdpclip` opens the clipboard right after every change, and
  `OpenClipboard` fails while it does; every read and write now retries for
  up to 200 ms.
