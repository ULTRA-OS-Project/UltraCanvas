- **UltraIM: a file that is a picture goes with a preview picture.** ChatCube
  Direct now makes one (through the application's preview maker) for a file
  whose type is an image - an SVG drawing, which goes as a file - as it does
  for a photo or a video, and names `.svg` / `.svgz` as `image/svg+xml`; the
  Telegram provider sends such a document with its preview (inputThumbnail)
  too, so the other side's bubble shows the drawing before the file is fetched.
