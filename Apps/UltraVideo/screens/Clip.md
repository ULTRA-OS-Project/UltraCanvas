One segment on a track: video, still image, colour card, or audio. It has a name band, badges, a filmstrip or waveform, and trim handles.

**Consumer provides:** a clip (`ClipModel`) with its start and duration on the timeline, source in/out, speed, mute and effect flags. A thumbnail provider fills the filmstrip.

- Anatomy: a 3 px top stripe in the edge colour, a `clip-band-h` band (name, effect icons, mute, speed badge, duration badge), then the content.
- States: rest, hover (trim grips appear), selected (2 px `accent` ring), muted (`muted-hatch` scrim + crossed speaker), locked (`opacity-disabled`), ghost while dragged, and trimming (the active edge turns `accent` and the unused source shows as `trim-overflow`).
- A still image shows the motion icon. A colour card shows its colour instead of a filmstrip.
- Don't stretch thumbnails. Frames keep 16:9 at the lane height and are clipped at the clip's right edge.
