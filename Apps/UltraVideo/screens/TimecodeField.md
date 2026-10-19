A field for entering and showing `HH:MM:SS:FF` timecode, plus the large readout used in the transport.

**Consumer provides:** the value in frames, the frame rate and an `onCommit` handler.

- Click selects a segment; `↑`/`↓` step it and `Tab` moves to the next segment. Typed digits fill from the right, and leading zeros are dimmed with `ink-faint`.
- `+`/`−` followed by a number enters a relative offset (`+12` = 12 frames later).
- Invalid values get a `danger` border, and the last valid value is restored on `Esc`.
