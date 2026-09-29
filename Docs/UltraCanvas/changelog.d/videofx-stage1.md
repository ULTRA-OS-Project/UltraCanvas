- **VideoFX is implemented (stage 1).** Until now the module was a
  specification: a README describing 264 functions, no sources, no build
  target. `VideoFX/` is now a built, headless module on FFmpeg (4.4 to 8.x),
  wrapped behind its own types - no FFmpeg header reaches a caller:
  - **Inspection:** `VideoFX_Probe` (container, duration, tags, every stream,
    display rotation), `VideoFX_ExtractFrame` / `VideoFX_ExtractThumbnails`
    (upright RGBA, scaled to fit), `VideoFX_SaveFrameImage` (PNG / JPEG).
  - **A segment timeline:** `VideoFX_Export` plays file ranges, colour cards
    and test patterns one after another, each with its own trim, speed
    (0.25-4x, pitch kept) and effects, fits them to one size / rate / sample
    format (letterbox, fill or stretch) and encodes MP4, MOV, MKV, WebM, AVI,
    animated GIF, MP3, M4A, WAV, FLAC or OGG. Picture and sound stay in sync
    across joins: a stream that runs short is padded (last frame / silence).
  - **26 typed effects** (brightness, contrast, saturation, gamma, exposure,
    hue, temperature, grayscale, sepia, invert, 3D LUT with strength, blur,
    sharpen, denoise, vignette, quarter turns, free rotation, flips, crop,
    fade in / out, volume, EBU R128 loudness), validated before anything is
    written; filter text is dot-decimal under any locale.
  - One-line helpers `VideoFX_Transcode`, `Trim`, `ApplyEffects`,
    `Concatenate`, `ExtractAudio`, `GenerateTestClip`; `VideoFX_TrimLossless`
    cuts by stream copy; `VideoFXExportJob` runs an export on a worker thread
    with progress and cancel. A failed or cancelled export removes its
    partial file.
  - A `videofx` command-line tool (`info`, `frame`, `transcode`, `trim`,
    `concat`, `effects`, `testclip`).
  - Without FFmpeg the module still builds, from a stub whose calls return
    `VideoFXResult::NotAvailable`, so applications need no `#ifdef`.
  - `Tests/VideoFXTest.cpp` (116 checks) covers the filter-text translation
    and the engine end to end on clips it generates itself. The Linux CI rows
    now install FFmpeg's development packages so it runs there.
  - The demo's module list shows VideoFX as partially implemented; transitions,
    overlays, keyframes, multi-track mixing and project files are the next
    stages (`Docs/Modules/VideoFX/README.md`, `Masterfile_modules.md` §16).
