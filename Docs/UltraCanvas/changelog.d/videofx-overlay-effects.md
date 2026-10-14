- **VideoFX 0.14.0: effects on image and video overlays.** A new field,
  `VideoFXOverlay::effects`, gives an overlay's own picture a look. It works
  on a logo or a picture-in-picture video, without touching the picture
  under it.
  - Colour and detail effects only: brightness, contrast, saturation,
    gamma, exposure, hue, temperature, greyscale, sepia, invert, blur,
    sharpen, denoise and vignette.
  - They are applied in order, before the overlay is sized, in its own
    pixels.
  - The overlay's transparency is kept. Its alpha is split off before the
    effects and merged back after them
    (`Internal::BuildOverlayEffectGraph`).
  - Refused with `InvalidArgument` when the export is planned:
    - geometry, fades, LUTs and sound effects on an overlay;
    - any effect on a text overlay.
  - The export, the preview and project files (`overlays[].effects`) all
    carry them.
  - **Proxies keep the look of a blur.** `VideoFX_UseProxies` now scales a
    `Blur`'s radius to the proxy's picture, as it already scaled a `Crop`'s
    rectangle, for both segments and picture-in-picture videos. Before,
    over a proxy half as tall, a blur looked twice as strong in the preview
    as in the export.
  - Tested in `VideoFXTest`:
    - the filter text, and what is accepted and refused;
    - a half-transparent picture turned grey with its transparent half
      still showing what is under it, in the preview and the export;
    - a sepia picture-in-picture video;
    - a project round trip;
    - blur radii scaled on proxies.
