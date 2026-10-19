The time scale on top of the timeline, with adaptive ticks, timecode labels, markers, the work-area range, beat ticks and the playhead handle.

**Consumer provides:** pixels per second, the frame rate, the scroll offset, markers, an optional range, and optional beats.

- Choose the minor tick step from 1 frame, 5 frames, 0.5, 1, 2, 5, 10, 30 or 60 s so that minor ticks are at least 8 px apart. Major ticks are at least 64 px apart.
- Use `MM:SS` labels when the major step is 1 s or more, and full `HH:MM:SS:FF` below that.
- Markers sit in the top strip with their label, and a 1 px line runs down the ruler. A double-click edits the label and colour.
- Clicking or dragging the ruler moves the playhead. Hovering shows a `ink-faint` hairline and a readout.
