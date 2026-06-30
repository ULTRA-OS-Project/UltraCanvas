# UltraCanvas — Dependencies & Third-Party Libraries

Third-party libraries used by the **UltraCanvas** core and every additional
**ULTRA OS module** listed in the demo app, grouped per module. For each
component the libraries are listed per platform (Linux / macOS / Windows).

**Legend**

- **(bundled)** — vendored in-tree (compiled from sources shipped with UltraCanvas).
- **(optional)** — built only when the dependency / feature is present.
- **→ native** — the OS-native API/framework the cross-platform library wraps.
- **(core only)** — the module needs no extra third party beyond the UltraCanvas core.

> QOI and XAR are intentionally **not** listed: those formats are implemented by
> UltraCanvas itself and are not third-party dependencies.

Source of truth: every `CMakeLists.txt` (root, `UltraCanvas/`,
`Plugins/Vector/{CDR,XAR}`) plus the third-party `#include`s across the source
tree. Mirrors the in-app table at
**Dependencies & Third Party → Dependencies & Third Party**
(`Apps/DemoApp/UltraCanvasDependenciesExamples.cpp`).

---

## UltraCanvas (core)

| Purpose | Linux | macOS | Windows |
|---|---|---|---|
| 2D graphics rendering | Cairo | Cairo | Cairo |
| Text layout & shaping | Pango, HarfBuzz, FreeType | Pango, HarfBuzz, FreeType | Pango, HarfBuzz, FreeType |
| Font discovery | FontConfig | CoreText | FontConfig |
| Native windowing & cursors | X11, Xcursor | Cocoa, QuartzCore | Win32 (gdi32, user32) |
| Native dialogs & printing | GTK 3 | Cocoa (AppKit) | Win32 (comdlg32) |
| Core utilities | GLib | GLib | GLib |
| XML parsing (SVG/config/ODS) | TinyXML2 | TinyXML2 | TinyXML2 |
| String formatting | fmt | fmt | fmt |
| Image processing & resize | libvips | libvips | libvips |
| Bitmap codecs (via libvips) | libpng, libjpeg-turbo, libtiff, libwebp | libpng, libjpeg-turbo, libtiff, libwebp | libpng, libjpeg-turbo, libtiff, libwebp |
| GIF / HEIF / AVIF (via libvips) | giflib, libheif | giflib, libheif | giflib, libheif |
| BMP / PCX coders (via libvips) | ImageMagick | ImageMagick | ImageMagick |
| Audio playback & capture | miniaudio (bundled) → ALSA / PulseAudio | miniaudio (bundled) → CoreAudio | miniaudio (bundled) → WASAPI |
| Video playback & capture | GStreamer (optional) | AVFoundation (optional) | Media Foundation (optional) |
| OpenGL 3D surface | OpenGL, EGL / GLX (optional) | OpenGL, CGL (optional) | OpenGL, GLEW / WGL (optional) |
| QR / barcode decoding | zbar (optional) | zbar (optional) | zbar (optional) |
| QR code generation | qrcodegen (bundled) | qrcodegen (bundled) | qrcodegen (bundled) |
| ZIP for ODS / XLSX I/O | miniz (bundled) | miniz (bundled) | miniz (bundled) |
| FFT / spectrogram | KissFFT (bundled) | KissFFT (bundled) | KissFFT (bundled) |

---

## Optional plugins

### PDF plugin (optional)

| Purpose | Linux | macOS | Windows |
|---|---|---|---|
| PDF rendering | MuPDF, mujs, gumbo, jbig2dec / openjp2 | MuPDF, mujs, gumbo | MuPDF, mujs, gumbo |

### CDR plugin (optional)

| Purpose | Linux | macOS | Windows |
|---|---|---|---|
| CorelDRAW import | libcdr, librevenge, librevenge-stream, LCMS2 + ICU (optional) | libcdr, librevenge, librevenge-stream, LCMS2 + ICU (optional) | – (not available) |

---

## Demo application

| Purpose | Linux | macOS | Windows |
|---|---|---|---|
| JSON parsing | jsoncpp | jsoncpp | jsoncpp |
| App icon → .exe (build tool) | – (Windows only) | – (Windows only) | ImageMagick (`magick` CLI) |

---

## Additional ULTRA OS modules

### AudioFX module

| Purpose | Linux | macOS | Windows |
|---|---|---|---|
| Audio effects / decode | FFmpeg, libsndfile, FFTW3 | FFmpeg, libsndfile, FFTW3 | FFmpeg, libsndfile, FFTW3 |

### FileLoader module

| Purpose | Linux | macOS | Windows |
|---|---|---|---|
| No additional third party | (core only) | (core only) | (core only) |

### IODeviceManager module

| Purpose | Linux | macOS | Windows |
|---|---|---|---|
| Scanners / cameras / print (native OS) | SANE, V4L2, CUPS | ICA, AVFoundation | WIA, TWAIN, Media Foundation |

### PixelFX module

| Purpose | Linux | macOS | Windows |
|---|---|---|---|
| Image effects | libvips | libvips | libvips |

### Smart Home module

| Purpose | Linux | macOS | Windows |
|---|---|---|---|
| No additional third party | (core only) | (core only) | (core only) |

### Ultra AI module

| Purpose | Linux | macOS | Windows |
|---|---|---|---|
| No additional third party (uses UltraNet / UltraVault siblings) | (core only) | (core only) | (core only) |

### Ultra Net module

| Purpose | Linux | macOS | Windows |
|---|---|---|---|
| Core protocols (HTTP/WS/FTP/TLS/DNS) | libcurl, OpenSSL | libcurl, OpenSSL | libcurl, OpenSSL |
| Native sockets / platform glue | POSIX sockets (glibc) | BSD sockets, Network.framework | Winsock2 (ws2_32) |
| Extra protocols (SMTP/MQTT/SSH/gRPC/…) | plugin-supplied (libs tracked separately) | plugin-supplied (libs tracked separately) | plugin-supplied (libs tracked separately) |

> UltraNet's core (libcurl + OpenSSL) only covers the Tier-1 protocols; the
> SMTP / IMAP / MQTT / SSH / gRPC / LDAP / RTSP / … protocols are provided by
> plugins, whose backing libraries are tracked separately and not yet named.

### VideoFX module

| Purpose | Linux | macOS | Windows |
|---|---|---|---|
| Video effects / transcode | FFmpeg | FFmpeg | FFmpeg |

### VirtualFS module

| Purpose | Linux | macOS | Windows |
|---|---|---|---|
| Archive formats | libarchive, libmspack, wimlib | libarchive, libmspack, wimlib | libarchive, libmspack, wimlib |
| Compression | zlib, libzstd, liblz4, libbrotli | zlib, libzstd, liblz4, libbrotli | zlib, libzstd, liblz4, libbrotli |

---

*Part of ULTRA OS · Generated to accompany the UltraCanvas demo app's
"Dependencies & Third Party" screen.*
