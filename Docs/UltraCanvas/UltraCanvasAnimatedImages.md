# Animated Images (GIF / animated WebP)

UltraCanvas plays animated images the same way it plays video: a decoded
frame sequence stepped on the application's main-thread timer. Any format
whose libvips loader reports multiple pages **with per-frame delays**
animates — GIF and animated WebP today. Multi-page stills (TIFF, PDF)
expose pages but no delays and keep displaying as static images.

Animation works out of the box in:

- **`UltraCanvasImageElement`** — animated images auto-play on load.
- **`UltraCanvasMediaViewer`** — GIFs play in the viewer surface; zoom, pan,
  rotation and mirroring apply live to the running animation. Colour
  adjustments (gamma, brightness, per-channel, sharpen) freeze playback on
  the current frame while active, because they are baked into a single
  composited pixmap.
- **`UltraCanvasImageViewer`** (the lightbox used by the markdown renderer
  and the Album photo viewer) — animated images auto-play in the zoom / pan
  surface; wheel-zoom and drag-pan apply to the running animation.

## Architecture

The implementation is split into three layers, mirroring the video stack
(`IVideoBackend` → `UltraCanvasVideoPlayer` → `UltraCanvasVideoPlayerElement`):

| Layer | Class | Role |
|---|---|---|
| Decode | `UCImageRaster::GetAnimation()` | libvips multi-page load (`n=-1`); every frame becomes a fully composited `UCPixmap` (GIF frame disposal is resolved by the loader) |
| Data | `UCImageAnimation` | shared, cached frame sequence: pixmaps + per-frame delays + loop count |
| Playback | `UCImageAnimationController` | steps frames with one-shot app timers (the same main-thread timer mechanism the video player element uses for its frame ticks) |

`UCImageAnimation` is decoded lazily on first request and cached on the
`UCImageRaster`, so every element showing the same file shares one decoded
copy (the image cache accounts for the frame memory). Each element owns its
own `UCImageAnimationController`, so two views of the same GIF can be at
different positions or paused independently.

The controller is media-agnostic: anything expressible as "pixmaps +
delays + loop count" (sprite strips, generated sequences) can be driven by
it. Video keeps its own clock — the audio pipeline paces its frames — so it
is not routed through this class.

### Timing rules

- Encoded delays below 20 ms display for 100 ms (the browser convention many
  GIFs rely on); missing delays default to 100 ms.
- The file's loop count is honoured (`0` = repeat forever). A finished finite
  animation freezes on its last frame; `Play()` restarts it.
- Animations whose fully decoded frames would exceed 512 MB are not decoded;
  the image displays as a still (frame 0).

## UltraCanvasImageElement API

```cpp
auto gif = CreateImageElement("banner", 10, 10, 300, 200);
gif->LoadFromFile("animation.gif");     // auto-plays if animated

gif->IsAnimatedImage();                 // more than one frame?
gif->PauseAnimation();
gif->PlayAnimation();
gif->StopAnimation();                   // pause + rewind to frame 0
gif->IsAnimationPlaying();

// Show only the first frame (may also be called before loading):
gif->SetAnimationEnabled(false);

// Advanced control: frame stepping, loop override, end notification.
auto& ctl = gif->GetAnimationController();
ctl.SetCurrentFrameIndex(3);
ctl.SetLoopForever(true);
ctl.onEnded = [] { /* finite loop count exhausted */ };
```

## Lower-level access

```cpp
UCImagePtr img = UCImage::Get("animation.gif");
if (img->IsAnimated()) {
    UCImageAnimationPtr anim = img->GetAnimation();   // lazy full decode
    int frames = anim->GetFrameCount();
    int delay0 = anim->GetFrameDelayMs(0);
    UCPixmapPtr frame0 = anim->GetFramePixmap(0);     // draw with DrawPixmap

    UCImageAnimationController ctl;                   // own playback clock
    ctl.SetAnimation(anim);
    ctl.onFrameChanged = [&] { /* repaint */ };
    ctl.Play();
}
```

`GetFrameCount()` is available directly on the image (from the header
metadata) without decoding the frames; `GetAnimation()` is what triggers the
full decode.
