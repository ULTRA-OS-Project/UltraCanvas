The music lane: one bar for a song or playlist, with a waveform, fade-in and fade-out ramps, song boundaries with crossfades, and beat marks.

**Consumer provides:** the song list with start, duration and crossfade lengths, the fade lengths, and a peaks provider. Beats come from beat detection.

- Draw peaks in `waveform` on `clip-audio`, mirrored around the centre. The volume line is `ink` at 1.25 px. Fade regions are shaded with `muted-hatch`.
- Fade knobs (8 px `ink` discs) appear on hover or selection at the top corners. Drag them to set the fade length.
- Crossfades between songs show as a shaded crossing with a 2 px `clip-audio-edge` boundary.
