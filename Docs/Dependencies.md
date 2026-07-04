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

## Library links — websites & source code

Every third-party library used across the UltraCanvas core and the ULTRA OS
modules, with its **official homepage** and **canonical source / Git
repository**. In the demo app's documentation viewer these links are clickable
and open in your system browser. Operating-system frameworks (Cocoa, CoreText,
Media Foundation, Winsock, …) ship with the OS and have no public source
repository, so their *Source code* is marked **—** and the website points to the
vendor's developer documentation.

| Library | Website | Source code |
|---|---|---|
| ALSA | [alsa-project.org](https://www.alsa-project.org/) | [github.com/alsa-project/alsa-lib](https://github.com/alsa-project/alsa-lib) |
| AVFoundation | [developer.apple.com](https://developer.apple.com/documentation/avfoundation) | — |
| brotli | [brotli.org](https://www.brotli.org/) | [github.com/google/brotli](https://github.com/google/brotli) |
| Cairo | [cairographics.org](https://www.cairographics.org/) | [gitlab.freedesktop.org/cairo/cairo](https://gitlab.freedesktop.org/cairo/cairo) |
| CGL | [developer.apple.com](https://developer.apple.com/library/archive/documentation/GraphicsImaging/Conceptual/OpenGL-MacProgGuide/opengl_intro/opengl_intro.html) | — |
| Cocoa / AppKit | [developer.apple.com](https://developer.apple.com/documentation/appkit) | — |
| CoreAudio | [developer.apple.com](https://developer.apple.com/documentation/coreaudio) | — |
| CoreText | [developer.apple.com](https://developer.apple.com/documentation/coretext/) | — |
| CUPS | [openprinting.github.io/cups](https://openprinting.github.io/cups/) | [github.com/OpenPrinting/cups](https://github.com/OpenPrinting/cups) |
| EGL | [khronos.org/egl](https://www.khronos.org/egl/) | [github.com/KhronosGroup/EGL-Registry](https://github.com/KhronosGroup/EGL-Registry) |
| FFmpeg | [ffmpeg.org](https://ffmpeg.org/) | [git.ffmpeg.org/ffmpeg.git](https://git.ffmpeg.org/ffmpeg.git) |
| FFTW3 | [fftw.org](https://www.fftw.org/) | [github.com/FFTW/fftw3](https://github.com/FFTW/fftw3) |
| fmt | [fmt.dev](https://fmt.dev/) | [github.com/fmtlib/fmt](https://github.com/fmtlib/fmt) |
| FontConfig | [freedesktop.org/wiki/Software/fontconfig](https://www.freedesktop.org/wiki/Software/fontconfig/) | [gitlab.freedesktop.org/fontconfig/fontconfig](https://gitlab.freedesktop.org/fontconfig/fontconfig) |
| FreeType | [freetype.org](https://freetype.org/) | [gitlab.freedesktop.org/freetype/freetype](https://gitlab.freedesktop.org/freetype/freetype) |
| giflib | [giflib.sourceforge.net](https://giflib.sourceforge.net/) | [sourceforge.net/projects/giflib](https://sourceforge.net/projects/giflib/) |
| GLEW | [glew.sourceforge.net](https://glew.sourceforge.net/) | [github.com/nigels-com/glew](https://github.com/nigels-com/glew) |
| GLib | [docs.gtk.org/glib](https://docs.gtk.org/glib/) | [gitlab.gnome.org/GNOME/glib](https://gitlab.gnome.org/GNOME/glib) |
| GLX | [registry.khronos.org/OpenGL](https://registry.khronos.org/OpenGL/) | [github.com/KhronosGroup/OpenGL-Registry](https://github.com/KhronosGroup/OpenGL-Registry) |
| GStreamer | [gstreamer.freedesktop.org](https://gstreamer.freedesktop.org/) | [gitlab.freedesktop.org/gstreamer/gstreamer](https://gitlab.freedesktop.org/gstreamer/gstreamer) |
| GTK 3 | [gtk.org](https://www.gtk.org/) | [gitlab.gnome.org/GNOME/gtk](https://gitlab.gnome.org/GNOME/gtk) |
| gumbo | [github.com/google/gumbo-parser](https://github.com/google/gumbo-parser) | [github.com/google/gumbo-parser](https://github.com/google/gumbo-parser) |
| HarfBuzz | [harfbuzz.github.io](https://harfbuzz.github.io/) | [github.com/harfbuzz/harfbuzz](https://github.com/harfbuzz/harfbuzz) |
| ICA (ImageCaptureCore) | [developer.apple.com](https://developer.apple.com/documentation/imagecapturecore) | — |
| ICU | [icu.unicode.org](https://icu.unicode.org/) | [github.com/unicode-org/icu](https://github.com/unicode-org/icu) |
| ImageMagick | [imagemagick.org](https://imagemagick.org/) | [github.com/ImageMagick/ImageMagick](https://github.com/ImageMagick/ImageMagick) |
| jbig2dec | [jbig2dec.com](https://jbig2dec.com/) | [github.com/ArtifexSoftware/jbig2dec](https://github.com/ArtifexSoftware/jbig2dec) |
| jsoncpp | [open-source-parsers.github.io](https://open-source-parsers.github.io/jsoncpp-docs/doxygen/index.html) | [github.com/open-source-parsers/jsoncpp](https://github.com/open-source-parsers/jsoncpp) |
| KissFFT | [github.com/mborgerding/kissfft](https://github.com/mborgerding/kissfft) | [github.com/mborgerding/kissfft](https://github.com/mborgerding/kissfft) |
| LCMS2 (Little CMS) | [littlecms.com](https://www.littlecms.com/) | [github.com/mm2/Little-CMS](https://github.com/mm2/Little-CMS) |
| libarchive | [libarchive.org](https://www.libarchive.org/) | [github.com/libarchive/libarchive](https://github.com/libarchive/libarchive) |
| libcdr | [wiki.documentfoundation.org](https://wiki.documentfoundation.org/DLP/Libraries/libcdr) | [github.com/LibreOffice/libcdr](https://github.com/LibreOffice/libcdr) |
| libcurl | [curl.se/libcurl](https://curl.se/libcurl/) | [github.com/curl/curl](https://github.com/curl/curl) |
| libheif | [github.com/strukturag/libheif](https://github.com/strukturag/libheif) | [github.com/strukturag/libheif](https://github.com/strukturag/libheif) |
| libjpeg-turbo | [libjpeg-turbo.org](https://libjpeg-turbo.org/) | [github.com/libjpeg-turbo/libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo) |
| libmspack | [cabextract.org.uk/libmspack](https://www.cabextract.org.uk/libmspack/) | [github.com/kyz/libmspack](https://github.com/kyz/libmspack) |
| libpng | [libpng.org](http://www.libpng.org/pub/png/libpng.html) | [github.com/pnggroup/libpng](https://github.com/pnggroup/libpng) |
| librevenge | [sourceforge.net/p/libwpd](https://sourceforge.net/p/libwpd/wiki/librevenge/) | [sourceforge.net/p/libwpd/librevenge](https://sourceforge.net/p/libwpd/librevenge/) |
| libsndfile | [libsndfile.github.io](https://libsndfile.github.io/libsndfile/) | [github.com/libsndfile/libsndfile](https://github.com/libsndfile/libsndfile) |
| libtiff | [simplesystems.org/libtiff](http://www.simplesystems.org/libtiff/) | [gitlab.com/libtiff/libtiff](https://gitlab.com/libtiff/libtiff) |
| libvips | [libvips.org](https://www.libvips.org/) | [github.com/libvips/libvips](https://github.com/libvips/libvips) |
| libwebp | [developers.google.com/speed/webp](https://developers.google.com/speed/webp) | [chromium.googlesource.com/webm/libwebp](https://chromium.googlesource.com/webm/libwebp/) |
| lz4 | [lz4.org](https://lz4.org/) | [github.com/lz4/lz4](https://github.com/lz4/lz4) |
| Media Foundation | [learn.microsoft.com](https://learn.microsoft.com/en-us/windows/win32/medfound/microsoft-media-foundation-sdk) | — |
| miniaudio | [miniaud.io](https://miniaud.io/) | [github.com/mackron/miniaudio](https://github.com/mackron/miniaudio) |
| miniz | [github.com/richgel999/miniz](https://github.com/richgel999/miniz) | [github.com/richgel999/miniz](https://github.com/richgel999/miniz) |
| MuPDF | [mupdf.com](https://mupdf.com/) | [github.com/ArtifexSoftware/mupdf](https://github.com/ArtifexSoftware/mupdf) |
| mujs | [mujs.com](https://mujs.com/) | [codeberg.org/ccxvii/mujs](https://codeberg.org/ccxvii/mujs) |
| Network.framework | [developer.apple.com](https://developer.apple.com/documentation/network) | — |
| OpenGL | [opengl.org](https://www.opengl.org/) | [github.com/KhronosGroup/OpenGL-Registry](https://github.com/KhronosGroup/OpenGL-Registry) |
| OpenJPEG (openjp2) | [openjpeg.org](https://www.openjpeg.org/) | [github.com/uclouvain/openjpeg](https://github.com/uclouvain/openjpeg) |
| OpenSSL | [openssl.org](https://www.openssl.org/) | [github.com/openssl/openssl](https://github.com/openssl/openssl) |
| Pango | [pango.gnome.org](https://pango.gnome.org/) | [gitlab.gnome.org/GNOME/pango](https://gitlab.gnome.org/GNOME/pango) |
| PulseAudio | [freedesktop.org/wiki/Software/PulseAudio](https://www.freedesktop.org/wiki/Software/PulseAudio/) | [gitlab.freedesktop.org/pulseaudio/pulseaudio](https://gitlab.freedesktop.org/pulseaudio/pulseaudio) |
| qrcodegen | [nayuki.io](https://www.nayuki.io/page/qr-code-generator-library) | [github.com/nayuki/QR-Code-generator](https://github.com/nayuki/QR-Code-generator) |
| QuartzCore | [developer.apple.com](https://developer.apple.com/documentation/quartzcore) | — |
| SANE | [sane-project.org](http://www.sane-project.org/) | [gitlab.com/sane-project/backends](https://gitlab.com/sane-project/backends) |
| TinyXML2 | [leethomason.github.io/tinyxml2](https://leethomason.github.io/tinyxml2/) | [github.com/leethomason/tinyxml2](https://github.com/leethomason/tinyxml2) |
| TWAIN | [twain.org](https://www.twain.org/) | [github.com/twain](https://github.com/twain) |
| V4L2 (Video4Linux2) | [docs.kernel.org](https://docs.kernel.org/userspace-api/media/v4l/v4l2.html) | [git.linuxtv.org/v4l-utils.git](https://git.linuxtv.org/v4l-utils.git) |
| WASAPI | [learn.microsoft.com](https://learn.microsoft.com/en-us/windows/win32/coreaudio/wasapi) | — |
| WGL | [learn.microsoft.com](https://learn.microsoft.com/en-us/windows/win32/opengl/wgl-and-windows-reference) | — |
| WIA | [learn.microsoft.com](https://learn.microsoft.com/en-us/windows/win32/wia/-wia-startpage) | — |
| wimlib | [wimlib.net](https://wimlib.net/) | [wimlib.net/git/wimlib](https://wimlib.net/git/wimlib) |
| Win32 API (gdi32 / user32 / comdlg32) | [learn.microsoft.com](https://learn.microsoft.com/en-us/windows/win32/api/) | — |
| Winsock2 (ws2_32) | [learn.microsoft.com](https://learn.microsoft.com/en-us/windows/win32/winsock/windows-sockets-start-page-2) | — |
| X11 (libX11) | [x.org](https://www.x.org/) | [gitlab.freedesktop.org/xorg/lib/libX11](https://gitlab.freedesktop.org/xorg/lib/libX11) |
| Xcursor | [x.org](https://www.x.org/) | [gitlab.freedesktop.org/xorg/lib/libXcursor](https://gitlab.freedesktop.org/xorg/lib/libXcursor) |
| zbar | [zbar.sourceforge.net](https://zbar.sourceforge.net/) | [github.com/mchehab/zbar](https://github.com/mchehab/zbar) |
| zlib | [zlib.net](https://zlib.net/) | [github.com/madler/zlib](https://github.com/madler/zlib) |
| zstd | [facebook.github.io/zstd](https://facebook.github.io/zstd/) | [github.com/facebook/zstd](https://github.com/facebook/zstd) |

> qrcodegen, miniz, KissFFT and miniaudio are **bundled** (vendored in-tree); the
> links above point to their upstream projects for reference and updates.

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
| Archive formats | libarchive | libarchive | libarchive |
| Compression | zlib; optional: libzstd, liblz4 | zlib; optional: libzstd, liblz4 | zlib; optional: libzstd, liblz4 |
| Planned providers (CHM/LIT, WIM, Brotli) | libmspack, wimlib, libbrotli (not yet wired) | libmspack, wimlib, libbrotli (not yet wired) | libmspack, wimlib, libbrotli (not yet wired) |

> The build degrades gracefully: configure succeeds without libarchive or
> zlib, the affected features are just compiled out with a warning. Zstandard
> and LZ4 are opt-in via `-DVIRTUALFS_USE_ZSTD=ON` / `-DVIRTUALFS_USE_LZ4=ON`
> (both default OFF). libmspack, wimlib, and libbrotli belong to the planned
> CHM/LIT, WIM, and Brotli providers and are not detected by the build yet.

---

*Part of ULTRA OS · Generated to accompany the UltraCanvas demo app's
"Dependencies & Third Party" screen.*
