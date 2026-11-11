- **Auto contrast counts with the framework's histogram model.** The stretch
  comes from `UltraCanvasHistogramModel` (`ClippedRange`, 0.2 % off each end per
  channel), the counting the Curves backdrop uses. Opaque pictures are
  stretched exactly as before; fully transparent pixels no longer count, so
  the colour hidden under a transparent area no longer sets the stretch.
