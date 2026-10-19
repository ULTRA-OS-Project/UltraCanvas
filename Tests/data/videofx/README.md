# VideoFX test data

- `astronaut-200.ppm` — NASA astronaut Eileen Collins, the "astronaut" sample
  image of scikit-image (`skimage.data.astronaut()`), scaled to 200 x 200 and
  stored as a binary PPM so the test reads it without any decoder. NASA
  photograph from the NASA Great Images database; no known copyright
  restrictions, in the public domain. Used by `VideoFXTest` to check the
  built-in face detector finds a real face where OpenCV's own
  `CascadeClassifier` (same model, `detectMultiScale(1.1, 3, minSize 20)` on
  its grey copy) finds it: at (66, 24), 43 x 43 pixels.
