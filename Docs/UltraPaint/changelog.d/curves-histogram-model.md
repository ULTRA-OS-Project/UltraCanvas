- **Curves shows the same histogram as the image viewer.** The backdrop is
  counted by the framework's `UltraCanvasHistogramModel`, as the viewer's Curves
  dialog counts it. The RGB backdrop now adds the red, green and blue values
  level by level - the values the RGB curve maps - instead of counting each
  pixel's average of the three. Transparent pixels are still left out.
