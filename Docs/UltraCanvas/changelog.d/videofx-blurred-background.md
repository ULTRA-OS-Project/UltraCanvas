- **VideoFX: portrait photos no longer lose two thirds of themselves.** A
  still image's framing is now `VideoFXSegment::imageFit` (and
  `VideoFXSlideshowOptions::imageFit`): `Cover`, `Contain` (black bars) or
  the new `BlurredBackground`, which shows the whole photo over a blurred,
  darkened, enlarged copy of itself - made once per photo from a 1/12-size
  render. The default `Auto` keeps `Cover` for 4:3, 3:2 and panoramic
  photos and switches to `BlurredBackground` for an image much taller than
  the frame, such as a portrait photo in a 16:9 slideshow. Pan and zoom move
  the sharp photo in front of its backdrop. Still images no longer take
  their framing from the video `fitMode`. `videofx slideshow --fit`.
