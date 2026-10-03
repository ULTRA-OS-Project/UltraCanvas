- **A renderer fix now reaches thumbnails already on disk.** The thumbnail
  disk cache judged an entry stale only by its source file's size and
  modification time, so a thumbnail drawn by a build with a renderer bug was
  served for as long as it kept being shown. The vector previews drawn at
  the fit squared before 2026-09-26 stayed specks in a corner at every size
  cached back then - in UltraFiler, Xara files looked right in a folder and
  broken in the History view. Each entry now records the build's renderer
  generation (`ThumbnailDiskCache::kRendererGeneration`, now 2; the entry
  header is format 2), and an entry of another generation is a miss that is
  deleted and redrawn. Bump the constant in any change that makes a
  thumbnail producer draw something different. Pinned by
  `ThumbnailDiskCacheTest`.
