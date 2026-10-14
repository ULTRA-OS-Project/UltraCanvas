- **`UltraCanvasVideoTimeline` 1.2.0: `SetDropTargetClip`.** An application
  that carries a drop of its own onto the timeline (UltraVideo drags an
  effect preset from its bin onto a clip) names the clip under the pointer
  and the element rings it with a dashed focus line until the name is
  cleared; `ClipAt` finds the clip, so the whole drag stays in the app's
  own event filter. The element's own drop path for files is unchanged.
