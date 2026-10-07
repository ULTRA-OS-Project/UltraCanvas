- **VideoFX: faces kept in shot through pan and zoom** (VideoFX 0.5.0).
  `VideoFXSegment::keepInView` lists regions of a photo (`VideoFXRect`,
  fractions of the image as shown, or `FromPixels`) that must stay in
  frame: the move's zoom is capped where the view could no longer hold them
  (with 15 % headroom), its pan narrowed around them, and every frame slid -
  never resized - to hold them, so they stay in shot through the whole move.
  A `Still` photo looks at them. VideoFX finds no faces itself: slideshows
  take `keepInView` per image, or ask the app's own detector
  (`findKeepInView` - UltraAI's vision analyser, the OS, a tap in the UI)
  once per photo.
  - `videofx slideshow ... --keep N:X,Y,W,H`.
