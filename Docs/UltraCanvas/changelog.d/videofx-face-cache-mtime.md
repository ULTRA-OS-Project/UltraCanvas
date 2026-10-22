- **VideoFX 0.16.2: a clip's faces are looked for afresh when its file
  changes.** The faces found in a clip (`keepFacesInView` under `Fill`) are
  remembered per file and range for the preview's sake; the file's size and
  write time are part of the key now, so a clip re-exported over an old one
  of the same name no longer frames to the faces of the old one.
  `VideoFXTest` exports a faceless clip under the name the late-face clip
  then takes, and the crop follows the new file's face.
