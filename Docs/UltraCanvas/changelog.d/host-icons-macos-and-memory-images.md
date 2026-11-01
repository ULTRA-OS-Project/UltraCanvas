- **macOS host file icons are the type's icon, never one file's.** The
  content-type branch of `UltraCanvasMacOSHostFileIcons.mm` was compiled out
  (its `ULTRACANVAS_HAS_UTTYPE` was only defined in the "Open with" backend),
  so every lookup called `iconForFile:` with the real path: one file's custom
  icon was cached as the icon of its whole type, and a folder was drawn with
  the startup disk's icon. The backend now detects the SDK itself and asks by
  `HostFileIconProbeName` only - `iconForContentType:` (folder, the type of
  the extension, or plain data), or `iconForFileType:` before macOS 11.
- **An image that arrives as bytes can no longer draw another one's
  picture.** In a build without libvips `UCImageRaster::LoadFromMemory` still
  named the image after the buffer's address, and so did
  `UCImageRaster::GetFromMemory` in every build: a freed buffer's address
  reused for another picture of the same size found the first picture in the
  pixmap or image cache. In-memory images are named by a counter in both
  builds now, and `GetFromMemory` keys by a hash of the bytes and checks a
  hit against the bytes the cached image kept. `ImageCacheFreshnessTest`
  reuses one buffer for two pictures and checks each is drawn as itself.
