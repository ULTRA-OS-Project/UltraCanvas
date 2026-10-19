Hover help, and the live readout that follows the pointer during drags.

**Consumer provides:** the text and an optional shortcut, or the readout value.

- A help tooltip uses `bg-300` + `shadow-menu`, appears after 500 ms and shows the shortcut in `uv-kbd`.
- A readout is inverted (`transition-fill`/`on-transition`), set in mono, has no delay and sits 6 px above or beside the pointer, never covering the edited edge.
