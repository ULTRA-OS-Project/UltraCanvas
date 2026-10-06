- **Saving under a name without an extension says what is wrong.** The
  extension picks the format, so a name saved under "All files" without one
  has none to pick. `UCRasterDocument::SaveToFile` passed such a name to
  libvips, which answered only that it was "not a known file format", and
  `UltraCanvasFileLoader::SaveVectorDocument` answered "No writer for .".
  Both now say the name has no extension and to add one or choose the file
  type, and write nothing. UltraPaint and ArtCreator relied on adding a
  default extension of their own instead; they no longer do (see their
  changelogs), since the save dialog adds the chosen type's.
