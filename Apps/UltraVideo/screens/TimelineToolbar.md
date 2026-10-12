The strip above the timeline holds tools, edit actions, mode toggles, the current timecode and the zoom control.

**Consumer provides:** the active tool, the toggle states (magnetic, snapping, beat marks), the zoom level and the playhead time.

- Groups from left to right: tools (V, B, T, Y), edit actions (split, delete, marker), modes (magnet, snap, beats), timecode in the centre, zoom on the right.
- An active tool or toggle is shown as `accent-soft` with an `accent` icon, and it must also set `aria-pressed` or the UltraCanvas equivalent.
- Every button is ghost and icon-only, with a tooltip giving the name and shortcut.
