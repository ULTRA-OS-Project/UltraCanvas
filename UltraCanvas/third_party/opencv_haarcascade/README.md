# OpenCV Haar cascade: frontal faces (vendored)

The trained frontal-face model VideoFX's built-in face detector runs
(`VideoFX_DetectFaces`, and slideshows keeping faces in shot). Only the data
is vendored — VideoFX evaluates it with its own code and links no OpenCV.

- File: `haarcascade_frontalface_alt.xml` — stump-based 20x20 gentle AdaBoost
  frontal face detector, created by Rainer Lienhart
- Upstream: https://github.com/opencv/opencv/tree/4.x/data/haarcascades,
  taken unmodified from the `opencv-python-headless` 4.10.0.84 wheel
  (`cv2/data/`)
- SHA-256: `6281df13459cc218ff047d02b2ae3859b12ff14a93ffe8952f7b33fad7b9697b`
- License: Intel License Agreement For Open Source Computer Vision Library, a
  3-clause BSD-style licence (see `LICENSE`; it is also the file's own header)

Do not edit the XML. VideoFX compiles a table generated from it —
`VideoFX/core/VideoFXFaceCascade.inc`, written by
`python3 scripts/generate_face_cascade.py` (`--check` confirms the two agree).
