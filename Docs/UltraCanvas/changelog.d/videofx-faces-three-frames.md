- **VideoFX 0.16.1: a clip's faces are looked for in three frames, not one.**
  `keepFacesInView` on a video segment under `Fill` read one frame from the
  middle of the clip's used range, so a person who walks into shot late, or
  is on one side at that moment, was missed or half-kept. The engine now
  reads frames a quarter, half and three quarters of the way through and
  keeps every face of the three, so the crop stays around all the places
  they were; still found once per clip and range. `VideoFXTest` renders a
  clip whose face appears only in its last third and reads the face back
  from the filled frame.
