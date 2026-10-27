- **HEIF and AVIF on macOS, without GPL code in the core.** The macOS libvips
  was built with `-Dheif=disabled` - the overlay port had no `heif` feature -
  so the Mac apps could neither read nor write HEIC or AVIF. The port gains a
  `heif` feature on vcpkg's `libheif`, and `MacOS/deps/vcpkg.json` asks for
  libheif without its default `hevc` feature, which is x265 (GPL-2.0): libvips
  is linked into the core every app links, and the framework is MIT. With
  libde265 (LGPL) and aom (BSD) the Mac apps read HEIC and read and write
  AVIF; they do not write HEIC.
- **DemoApp's performance page charts HEIF and AVIF only where they encode.**
  Like JPEG XL, each is left out of the chart unless `VipsCanEncode` and
  `VipsCanLoad` say this build handles it - libheif can be present with no
  encoder for a format (macOS for HEIC; a Linux without libheif's encoder
  plug-ins for both), and the column then only ever showed a failure.
  `Docs/Dependencies.md` lists libde265 and aom.
