- **The media viewer's Save image as saved nothing for an SVG.** The save
  dialog offered the shown file's own name, so a drawing came up as
  `diagram.svg` with "PNG image" as the type. The dialog returns the name as
  typed, libvips picks its writer from the extension, and it has no SVG
  writer: the save failed with `"diagram.svg" is not a known file format`,
  reported only in the small info line at the bottom of the viewer, so it
  looked as if Save did nothing. `UltraCanvasMediaViewer` now:
  - offers a name the viewer can write: the file's own when it is PNG, JPEG,
    WebP, TIFF, AVIF or BMP, otherwise its stem as a PNG (`diagram.png`);
  - as a last resort, after the dialog has applied the chosen type
    (`ApplySaveExtension`, 0.9.176), adds `.png` to a name that still has none of
    those extensions - added rather than swapped in, because what follows
    the last dot is not always a format - and asks before replacing a file
    of that name, which the dialog could not ask about;
  - shows an error message when a save fails, as well as the info line.
