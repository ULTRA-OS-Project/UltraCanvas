- **VideoFX: a frame scaled to RGBA no longer overruns its buffer.** The
  three places that hand libswscale a plain pixel vector as its destination
  (`VideoFX_ExtractFrame` and the thumbnails, `FitFrame`, the photo scaler
  of the export) sized it to the pixels exactly, and the scaler's
  vectorised paths write a few words past the last pixel they fill, as they
  may into FFmpeg's own padded frame buffers: a 200 x 600 frame overran by
  16 bytes and corrupted the heap (`free(): corrupted unsorted chunks` in
  `sws_freeContext`), found by valgrind under the new three-frame face
  test. Each buffer now carries `kSwsSlack` (64 bytes) and is trimmed back
  afterwards.
