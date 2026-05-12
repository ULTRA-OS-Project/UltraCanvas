# Bitmap Handling in GUI Programs

How modern GUI frameworks load, store, and draw raster images — and why.

## TL;DR

**Decode once at load, keep an in-memory pixel buffer, transform on-the-fly at redraw.**

Never re-decode the compressed source on every paint — JPEG/WebP/HEIC decoding is millisecond-scale and would tank your frame rate. The canonical in-memory buffer should be **32-bit BGRA / ARGB premultiplied**, not 24-bit RGB.

## The Pipeline

Most modern frameworks (Qt, GTK/Cairo, Skia/Chrome, macOS Core Graphics, Cairo + libvips) converge on the same flow:

```
[encoded bytes] ──decoder──► [pixel buffer in RAM] ──blit/upload──► [screen]
   .jpg/.png/.webp/.tiff       BGRA32 premultiplied
```

### 1. Decode once → canonical pixel buffer

The canonical buffer is almost always **32-bit, not 24-bit**. Reasons:

- **Alpha channel is needed** even for opaque JPEGs — the compositor blends them with backgrounds, anti-aliased borders, and masks.
- **32-bit alignment** is SIMD-friendly and matches GPU texture formats.
- **Premultiplied alpha** turns blending into one MAD per channel instead of a multiply-then-divide.

A JPEG gets decoded to `BGRA8888 premultiplied`, alpha = 255, even though the source has no alpha. You spend 33% more RAM than RGB24, but get a uniform fast path everywhere downstream.

### 2. Cache hierarchy

```
Source bytes   →   Decoded buffer   →   Display-resized buffer   →   GPU texture
(disk/HTTP)        (one per image)      (per zoom/rotation)          (if accelerated)
cheapest cache     core cache            invalidated on resize        VRAM
```

A well-built image element keeps the **decoded buffer** as its source of truth. Zoom, rotate, and filter operations are recomputed cheaply from that — or, better, done by the GPU/compositor at draw time.

### 3. On-the-fly at redraw

What happens every frame is just:

- A blit (CPU path: `pixman` / `cairo_paint`), **or**
- A textured quad draw (GPU path: OpenGL / Metal / D3D) with the source rect, dest rect, and a transform matrix.

Both are O(visible-pixels), not O(source-pixels), and both are essentially free. **No decoding, no allocation per frame.**

## When You Deviate From "Decode Once"

| Scenario | What changes |
| --- | --- |
| **Animated GIF / WebP / APNG** | Decode frame-by-frame on a timer; cache N frames LRU. |
| **Huge images (>50 MP, TIFF, raw)** | Tiled decode + tile cache; only decode tiles intersecting the viewport. libvips is built around this — never holds the whole image in RAM. |
| **Thumbnail grids (file managers)** | Decode small, downsample early, throw away the big buffer. Use the format's own thumbnail (EXIF, JPEG mini-image, HEIC thumb track) when present. |
| **HDR (AVIF / HEIC 10–12-bit)** | Either keep a 16-bit float buffer (correct, more RAM) or tonemap to 8-bit on load (cheap, lossy). |
| **SVG / vector** | Rasterize to a buffer at the *display* size; re-rasterize when the size changes. The "decoded buffer" is the rasterized bitmap. |
| **Memory pressure** | Mark decoded buffers as *discardable* — evict them under pressure and re-decode from the source bytes (which you keep). Chrome's image cache works this way. |

## The 24-bit Option, Specifically

Going to **packed RGB24** is the wrong move in 2026 because:

- Every blend / compositor path expects 4 bytes per pixel.
- 3-byte rows are unaligned, so the SIMD path is disabled and software blit ends up ~3× slower.
- No room for premultiplied alpha. You're going to need alpha somewhere — rounded corners, fades, masking.

The only place RGB24 still wins is **read-only static textures that never composite** (e.g., a background you `memcpy` once into a framebuffer). Not worth the special case in a general framework.

## Practical Recipe (Cairo + libvips)

For a framework like UltraCanvas (Cairo backend, libvips for decoding):

1. **Load**: hand the file to libvips → it returns a `VImage`. libvips is lazy and tile-based, so this doesn't necessarily pull all pixels into RAM yet.
2. **Materialize**: convert to `cairo_image_surface` with `CAIRO_FORMAT_ARGB32`, premultiplied. Cache this surface on the image element.
3. **Draw**: at paint time, set a transform matrix on the Cairo context (scale, rotate, translate) and `cairo_set_source_surface()` + `cairo_paint()`. No re-decode.
4. **Resize**: when display size changes by more than ~2×, consider downsampling once into a smaller surface to avoid Cairo's per-frame downsample cost.
5. **Memory pressure** (optional): release the cached surface but keep the path/bytes, and re-materialize on the next draw.

This is the same pattern GTK, WebKitGTK, and Firefox use. libvips' lazy/tiled decoding also gives you the huge-image escape hatch for free.

## Summary

| Concern | Right answer |
| --- | --- |
| Decode every redraw? | **No.** Decode once. |
| Store as RGB24? | **No.** Store as 32-bit premultiplied (B)(G)RA. |
| Apply zoom/rotate/filter at load? | **No.** Apply at draw time via a transform matrix. |
| Re-decode source if memory is tight? | **Yes**, that's the point of the source-bytes cache. |
| Tile big images? | **Yes**, for anything that doesn't fit comfortably in RAM. |
| Special-case animated formats? | **Yes** — frame decoder + timer + small frame cache. |
