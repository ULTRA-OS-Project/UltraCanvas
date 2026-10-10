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
- **`UltraCanvasAlbum` and `UltraCanvasSlideshow` show a picture saved over as
  it is now.** Both paint with `UCImage::Get()`, which never looks at the
  disk, so an edited photo kept its old picture for the life of the widget.
  New `UltraCanvasImageFileWatch` (`UltraCanvasImageFileWatch.h`): a view
  hands it the paths each paint drew - no I/O on the paint path, just a
  compare-and-swap of the list - and a worker thread checks those files every
  1.5 s; when one changed its cached copy is dropped and the view's callback
  runs on the UI thread. The album relayouts and repaints, the slideshow
  repaints. Only what is on screen is checked, the worker starts with the
  first picture drawn, and a file that fails to decode is not read again on
  every pass. Each watch judges a change against its own record of the
  files, so two views showing one picture both repaint - the first to drop
  the cached copy no longer takes the change away from the other.
  - New `UltraCanvasFileStamp.h` (header-only): `FileStamp` and
    `StampFile(path)`, a file's size and modification time. The image cache,
    the thumbnail disk cache (`ThumbnailDiskCache::SourceStamp` is now an
    alias of it), the watch and UltraFiler's preview pane had each grown a
    copy of these lines.
  - New `UCImage::RemoveFromCacheIfChanged(path)`: the check `GetFresh()`
    makes, without the reload - true when a cached copy was dropped because
    its file changed. Unlike `GetFresh()` it leaves a cached decode failure
    alone, so a background check that repeats does not decode a broken file
    over and over.
- **`UltraCanvasMediaViewer::IsPlayingMedia()`**: whether a shown video or
  sound is playing (the muted PreviewClip too). For a host deciding whether it
  may reopen the shown file, which restarts playback - UltraFiler's preview
  pane uses it to leave a playing video alone when the file changes.
- **The Filer's Ctrl shortcuts no longer warn at build time.** The Ctrl+A / C
  / X / V / D / F / P switch listed lowercase character literals beside the
  `UCKeys` letters; no backend delivers a lowercase key code (the Linux one
  upper-cases the keysym), so those cases were dead and clang reported each
  as "case value not in enumerated type". They are `UCKeys::A` and so on now,
  as in the text widgets. No change in behaviour.
