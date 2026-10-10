# UltraCanvasImageFileWatch

Keeps the pictures a view keeps drawing in step with their files, without
touching the disk on the paint path.

```cpp
#include "UltraCanvasImageFileWatch.h"

class PhotoWall : public UltraCanvasUIElement {
public:
    using UltraCanvasUIElement::UltraCanvasUIElement;

    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override {
        UltraCanvasUIElement::Render(ctx, dirtyRect);
        std::vector<UltraCanvasImageFileWatch::DrawnImage> drawn;
        for (const std::string& path : photos) {
            auto img = UCImage::Get(path);         // cheap: never looks at the disk
            if (img) ctx->DrawImage(*img, Rect2Dd(0, 0, 200, 150), ImageFitMode::Cover);
            // The version drawn: recorded in the image when it was read.
            drawn.push_back({path, img ? img->GetSourceStamp() : FileStamp{}});
        }
        watch.SetDrawnImages(std::move(drawn));    // no I/O here either
    }

    std::vector<std::string> photos;

private:
    // Runs on the UI thread after a drawn file was saved over and its cached
    // copy dropped. Raw `this` is safe: the watch never calls it once
    // destroyed, and it is destroyed with the element.
    UltraCanvasImageFileWatch watch{[this]() { RequestRedraw(); }};
};
```

## Why it exists

A paint path asks the shared image cache for its pictures with
`UCImage::Get()`, which is keyed by the path and never checks the file — that
is what makes it cheap enough for every tile of every frame. The price is that
a picture saved over keeps being drawn as it was. Checking from the paint path
is no answer: a stat per tile per frame is real cost on a local disk, and on a
network share it can hold the UI for as long as the server takes to answer.

So the view hands the watch what it drew at the end of each paint: each
picture's path and the version of the file it was read from
(`UCImage::GetSourceStamp()` — the file's size and modification time, recorded
in the image when it was read, so handing it over costs nothing). A worker
thread looks at those files every interval (1.5 s by default). A file that is
no longer the version drawn is dropped from the cache with
`UCImage::RemoveFromCacheIfChanged()` — the raster, its pixmaps, libvips'
cached operations — and the watch calls its callback on the UI thread. The
view repaints, `Get()` misses and reads the file as it is now, and the next
paint hands the new version over.

## Behaviour

- **Only what was drawn last is watched.** The cost follows what is on screen,
  not how many pictures the view holds, and the list stays watched while the
  view is idle: a picture saved over while nothing repaints is still noticed.
- **`SetDrawnImages` is cheap.** It sorts the list, keeps one entry per path
  (the version drawn last) and drops empty paths, and compares it with the
  last one — unchanged lists, the common case, stop there. No file is opened.
- **No thread until there is something to watch.** The worker starts with the
  first non-empty list, so a view that never draws a picture costs nothing.
- **Every view showing the picture hears of it.** Each watch compares the
  file with the version its own view drew (`UltraCanvasFileStamp.h`), not with
  the cache, so an album and a slideshow showing one photo both repaint — the
  first to drop the cached copy does not take the change away from the other,
  even when the file was saved over before either watch first looked.
- **Told once.** A change is reported once per new version of the file; a
  view that does not repaint (hidden) is not asked again every interval.
- **A picture that could not be drawn is watched too.** Hand it over with an
  invalid `FileStamp{}`; a file that appears or is fixed is a change.
- **A broken file is not read again and again.** `RemoveFromCacheIfChanged`
  only drops a file that changed; a picture that failed to decode stays failed
  until its file changes.
- **Teardown.** The destructor stops and joins the worker, and a callback
  already queued on the UI thread is dropped, not delivered.

| Call | Does |
|---|---|
| `UltraCanvasImageFileWatch(onChanged, intervalMs = 1500)` | `onChanged` runs on the UI thread after a watched file changed; intervals below 100 ms are raised to it |
| `SetDrawnImages(images)` | What the view drew this frame: `{path, version}` per picture, the version from `UCImage::GetSourceStamp()`; replaces the previous list, empty watches nothing |
| `GetDrawnImages()` | What is being watched now (sorted by path, one per path) |

`UltraCanvasAlbum` and `UltraCanvasSlideshow` use it. The Filer has a folder
watch of its own and reads its thumbnails with `UCImage::GetFresh()`, which
makes the same check and reloads in one call — right for a worker that is
about to read the file anyway, wrong for a paint path.
