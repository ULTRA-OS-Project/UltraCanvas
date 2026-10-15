# stb_image

`stb_image.h` v2.30 from https://github.com/nothings/stb, public domain
(or MIT, at the user's choice; see the end of the header). Unmodified.

Used by `libspecific/Cairo/StbRasterDecode.cpp` in a build without libvips
(`ULTRACANVAS_ENABLE_VIPS=OFF`, the WebAssembly `no-vips` and
`single-thread` variants) for JPEG, BMP and GIF (first frame). PNG goes
through cairo's own reader there; a build with libvips does not compile it.
