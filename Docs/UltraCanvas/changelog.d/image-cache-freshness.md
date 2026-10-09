- **A picture saved over is shown as it is now, not as it was.** The image
  cache `UCImage::Get()` is keyed by the path and never looked at the file
  again, and libvips' own operation cache under it is keyed by the file name,
  so a picture edited and saved over kept answering with its old content:
  the Filer rescanned the folder when the save landed, made the thumbnail
  again and got the old picture back.
  - New `UCImage::GetFresh(path)`: `Get()` for a caller that must see the
    file as it is now. Every image read from a file records the file's size
    and modification time, taken before the read; `GetFresh()` compares them
    with the file and, when it has changed - or the cached copy failed to
    decode, as a file caught half-written does - drops everything cached for
    the path and reads it again. One stat per call, so it is for thumbnail
    workers and viewers opening a file, not for paint paths; `Get()` is
    unchanged.
  - `UCImage::RemoveFromCache(path)` now also releases libvips' cached
    operations. Without that, the next load of a file saved over was handed
    the old file's header - its old width and height - so
    `UltraCanvasImageElement::LoadFromFile(path, true)` read a changed file
    at its old size.
  - A cached pixmap's key carries the source file's size and modification
    time, so a pixmap of a file's previous content that outlived its raster
    (the two are evicted on separate budgets) is never served for the file
    as it is now. The key is no longer printed into a 300-byte buffer, which
    cut long paths short and let two files deep in one folder share pixmaps.
  - The Filer's thumbnail workers, its dimensions probe and the media viewer
    read images with `GetFresh()`.
- **The thumbnail disk cache no longer keeps a thumbnail of a file's old
  content as the answer for its new content.** `ThumbnailDiskCache::Store`
  stamped an entry with the source's size and modification time when it was
  written, after the decode - so a thumbnail made from the old content (the
  image cache above, or a save landing mid-decode) was recorded as valid for
  the new file, and every later run showed the old picture until the file
  changed again. `Store(request, blob, madeFrom)` now takes the stamp taken
  before the decode (new `ThumbnailDiskCache::StampSource`) and stores
  nothing when the file no longer matches it. `kRendererGeneration` is 3, so
  every entry an earlier build may have recorded this way is made again once.
