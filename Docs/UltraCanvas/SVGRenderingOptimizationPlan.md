# SVG / Image Redraw Optimization Plan

**Status:** Phase 0 implemented · Phases 1–4 planned
**Scope:** the image rasterization pipeline (`UCImageRaster`, `g_PixmapsCache`), the
markdown inline image renderer (`UltraCanvasTextArea`), and the lightbox zoom
viewer (`UltraCanvasImageViewer` / `UltraCanvasZoomPanImage`).
**Last Modified:** 2026-07-11

This document records the redraw inefficiencies found while tracing the
"click an embedded SVG diagram → lightbox → zoom" flow (UltraAI module page in
the demo app), compares them with how mainstream GUI stacks solve the same
problems, and lays out a phased implementation plan. Each phase is
independently shippable and each later phase builds on the earlier ones.

---

## 1. The pipeline as analyzed

Every image draw funnels through the same path:

```
ctx->DrawImage(image, rect, fitMode)                 UltraCanvasRenderContext.h
  → UCImageRaster::GetPixmap(w, h, fitMode, scale)   libspecific/Cairo/ImageCairo.cpp
      → g_PixmapsCache lookup  (key: file?w:h:fitMode:scale, 50 MB shared LRU)
      → miss: CreatePixmap()   (rasterize at exactly w×h raw pixels)
```

- The **markdown inline image** draws via
  `UltraCanvasTextArea.cpp` (`LineLayoutType::MarkdownImage`) at the layout
  size — one cache entry, hit on every subsequent frame.
- The **lightbox viewer** (`UltraCanvasZoomPanImage::Render`) draws at
  `fitScale × zoom × intrinsicSize` — a *different* size on every wheel tick,
  so every zoom step is a cache miss and a full rasterization.
- `UCImage::Get()` caches per-file **metadata only** (`loadOnlyHeader = true`):
  before Phase 0, no decoded or parsed form of the file was retained anywhere.

## 2. Problems found

| # | Problem | Effect |
|---|---------|--------|
| P1 | SVG was re-read from disk and its XML re-parsed by librsvg (via libvips `thumbnail`) on **every** rasterization | Every zoom step paid file open + XML parse + CSS cascade before any pixel was drawn |
| P2 | The zoom viewer rasterizes the **whole document** at display size, even though only a window-sized part is visible | Cost grows O(zoom²); the 8 000 px safety cap exists only because of this, and it caps usable zoom at ~40× |
| P3 | Deep-zoom pixmaps (up to 8 000 px, i.e. 100–250 MB) are inserted into the shared 50 MB LRU | `AddToCache` evicts **everything else** (icons, thumbnails, unrelated UI pixmaps app-wide) to admit one single-use entry that the next insert evicts again |
| P4 | Every intermediate wheel-gesture zoom level is fully rasterized **synchronously on the UI thread** | Zoom latency is proportional to SVG complexity; a fast wheel gesture rasterizes ~10 sizes the user sees for one frame each |
| P5 | No asynchronous rasterization anywhere | Any complex/large source stalls the event loop; every mainstream stack (browsers, Android, iOS, Flutter) forbids decode on the UI thread |

### How other stacks solve these

- **Browsers / Skia:** parse once into a render tree / display list; rasterize
  only visible **tiles** on a raster thread pool; during pinch-zoom, scale the
  existing raster on the GPU and swap in crisp tiles when they land.
- **iOS/macOS:** `CATiledLayer` renders tiles on background threads while
  stale content stays visible scaled; async decode APIs
  (`UIImage.prepareForDisplay`) keep the main thread clean.
- **Android:** decode on the main thread is a StrictMode violation;
  `VectorDrawable` pre-compiles vector XML at build time and caches the
  rasterization at the current size.
- **GTK / Qt / Flutter:** retained parsed document (`RsvgHandle`,
  `QSvgRenderer`, compiled display list) rendered at any transform on demand.

The portable ingredients available to UltraCanvas are librsvg (already a
transitive dependency through libvips' svgload) and Cairo. The platform
frameworks themselves (CATiledLayer, VectorDrawable) are not linkable
libraries — what transfers is the architecture: **retain the parsed form,
rasterize only what is visible, off the UI thread, keeping stale pixels on
screen until crisp ones arrive.**

---

## 3. Phases

### Phase 0 — Parse-once SVG documents ✅ *implemented*

**Fixes P1.**

`UCSvgDocument` (`libspecific/Cairo/SvgDocumentCairo.h/.cpp`) retains the
parsed librsvg handle per file in a 16 MB LRU document cache
(`g_SvgDocsCache`). `UCImageRaster::Load` takes SVG dimensions from the parsed
document and `UCImageRaster::CreatePixmap` rasterizes from it — one file read
and one XML parse per file for the whole session, regardless of how many
sizes are rendered. Optional dependency (`HAS_LIBRSVG`); without librsvg the
old vips path is used unchanged.

Measured with the demo's `UltraAI.svg` (680×532, ~40 KB source):

| Operation | Cost |
|---|---|
| First parse (once per file) | 2.6 ms |
| Cached document lookup | 0.0004 ms |
| Render 400×300 from parsed doc | 11–25 ms |
| Render 4000×3000 from parsed doc | 54 ms |

Render calls on one document are mutex-serialized, deliberately, so Phase 4's
worker thread can share documents with the UI thread without new locking work.

### Phase 1 — Viewport-clipped rendering in the zoom viewer

**Fixes P2, and P3 as a side effect for the zoom path. Highest-value remaining step.**

`UltraCanvasZoomPanImage` currently computes `dispW×dispH` for the whole image
and asks `DrawImage` for a pixmap that big. Instead, for vector sources it
should render **only the visible element-sized region**:

- Add a render entry point that takes the *visible* rectangle in image space
  plus the current scale, and produces an element-sized pixmap:
  `UCSvgDocument::RenderRegion(double srcX, double srcY, double srcW, double srcH, int outW, int outH, float deviceScale)` —
  implemented as `cairo_translate(-srcX·s, -srcY·s); cairo_scale(s, s); rsvg_handle_render_document(...)`.
- `UltraCanvasZoomPanImage::Render` computes the visible image-space rect from
  `zoom`/`panX`/`panY` (it already has this math) and calls the region render
  for SVG-backed images; raster images keep the current path.
- **Do not insert** these viewport pixmaps into `g_PixmapsCache` — they are
  single-use by construction. Keep the last one as a member of the element
  (it is the "stale pixels" Phase 3 scales during gestures).
- Remove the 40× zoom clamp and the 8 000 px cap for the SVG path: cost per
  frame is now O(window size), constant in zoom.

Touches: `SvgDocumentCairo.h/.cpp`, `UltraCanvasImageViewer.cpp`.
No public API breaks; `UltraCanvasZoomPanImage` keeps its behavior for raster
images.

Acceptance: zooming a complex SVG to any level costs a constant per-frame
render (window-sized); memory use of the viewer is bounded by the window
size; the shared pixmap cache is untouched while zooming.

### Phase 2 — Cache hygiene for the shared pixmap LRU

**Fixes the remainder of P3.**

- Reject entries larger than a threshold (suggested: 1/8 of the cache budget,
  i.e. ~6 MB ≈ a 1250×1250 ARGB pixmap) in `UCCache::AddToCache`, or via a
  caller-side check in `GetPixmap`. Oversized pixmaps are returned to the
  caller but never admitted, so one lightbox session can no longer evict the
  application's entire icon/thumbnail cache.
- Optionally split budgets (small UI pixmaps vs. document-sized ones) if
  profiling still shows churn after Phase 1.

Touches: `UltraCanvasUtils.h` (UCCache) or `ImageCairo.cpp`. ~20 lines.

Acceptance: filling the viewer with deep-zoom renders leaves previously
cached small pixmaps (icons, markdown layout sizes) resident.

### Phase 3 — Gesture-time scaling with debounced crisp re-render

**Fixes P4 (perceived latency).**

The browser/CATiledLayer pattern, applied inside `UltraCanvasZoomPanImage`:

- On wheel input, **do not** rasterize. Update `zoom`/`pan`, redraw by drawing
  the *retained last pixmap* scaled to the new geometry (one `DrawPixmap`
  with a stretch — cheap, momentarily soft).
- Start/refresh a ~100–150 ms one-shot timer. When it fires (gesture idle),
  render the crisp viewport (Phase 1 call) and redraw.
- Double-click reset and window resize follow the same path.

This gives constant-time wheel response independent of document complexity,
even before any threading exists. Requires a one-shot timer facility on the
element (or reuse of the application's existing timer/animation tick).

Touches: `UltraCanvasImageViewer.cpp` only.

Acceptance: continuous wheel zoom on a complex SVG never blocks the event
loop for a full render per tick; a crisp frame appears within ~150 ms of the
gesture pausing.

### Phase 4 — Background rasterization worker

**Fixes P5, generalizes Phase 3.**

- A single worker thread (or small pool) owns a queue of render jobs:
  `{document, viewport, outW, outH, generation}`.
- The viewer submits a job on gesture idle instead of rendering inline; each
  new submission bumps `generation`, and the worker discards results whose
  generation is stale (supersede-don't-cancel is sufficient).
- Completed pixmaps are handed to the UI thread (posted callback →
  `RequestRedraw()`); Cairo image surfaces created and finished on the worker
  and only *used* on the UI thread are safe, and `UCSvgDocument`'s render
  mutex (Phase 0) already serializes handle access.
- Once the machinery exists, the markdown renderer can reuse it for its
  block images (placeholder first frame → crisp swap), matching how
  browsers/Android treat all decode work.

Touches: new small `libspecific/Cairo/RasterWorker.h/.cpp` (or equivalent),
`UltraCanvasImageViewer.cpp`; later `UltraCanvasTextArea.cpp` opt-in.

Acceptance: with an artificially heavy SVG (e.g. 10⁵ elements), the viewer
window stays responsive (moves, repaints, Esc works) while a crisp frame is
being produced.

### Phase 5 — Optional / future work

Only if profiling after Phases 1–4 shows a need:

- **Tiling** (true CATiledLayer equivalent): split the viewport into tiles so
  panning at high zoom only renders newly exposed strips, and partially
  arrived tiles can be shown. Phase 1's region render is the building block.
- **Display-list caching**: render a document once into a
  `cairo_recording_surface_t` and replay it per raster, caching the
  librsvg→cairo translation as well as the parse. Worthwhile only for
  documents rendered very frequently at many sizes.
- **Pre-compiled shipped assets** (the VectorDrawable idea): for assets that
  ship with an application (e.g. the demo's module diagrams), generate a
  serialized form or baseline PNG at build time.
- **Compressed raster caches** (QOI/LZ4): only relevant for many-small-reused
  rasters (albums, thumbnail farms) under memory pressure — explicitly *not*
  for the zoom path, whose large pixmaps are single-use and never worth
  compressing.
- **In-memory source bytes for non-SVG formats** (`loadOnlyHeader = false`
  heuristics): removes per-rasterization disk reads for small raster sources;
  low value while the OS page cache is warm, but cheap.

---

## 4. Recommended order and dependencies

```
Phase 0  parse-once documents            DONE
Phase 1  viewport-clipped rendering      ← next; biggest win, no new infrastructure
Phase 2  cache admission threshold       independent, trivial, any time
Phase 3  gesture scaling + debounce      builds on Phase 1's retained last-pixmap
Phase 4  background raster worker        builds on Phase 3's job shape + Phase 0's mutex
Phase 5  tiling / display lists / etc.   only if profiling justifies
```

Phases 1–3 are pure wins with no new threads and small, local diffs; Phase 4
introduces the only real complexity (job lifetime, supersession, UI-thread
handoff) and should land once the synchronous path is already viewport-sized
and debounced, so the worker only ever renders window-sized surfaces.
