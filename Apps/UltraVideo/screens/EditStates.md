Transient feedback states during editing: reordering, trimming with snapping, file drop, rubber-band selection and the blade.

**Consumer provides:** nothing beyond the timeline. These are states of the Timeline element and are documented here so they are implemented consistently.

- Reorder: the ghost uses `opacity-ghost` + `shadow-drag` and is lifted 6 px. An `accent` insertion marker with triangles shows the target, and neighbours slide in `duration-base`.
- Snap: a 1 px `focus` line through every lane with a diamond on the ruler, shown only while snapped.
- File drop: the lane gets `drop-soft` with a dashed `focus` outline, and a `focus` insertion marker plus a tooltip with the count and time.
- Rubber band: a 1 px `focus` border on `drop-soft`. Clips it touches are selected live.
- Blade: an `ink` hairline follows the pointer over the clip it would cut.
