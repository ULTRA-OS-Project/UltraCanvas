UltraVideo is the visual language of the ULTRA OS video editor and of its core element, `UltraCanvasVideoTimeline`. It targets prosumer editors on desktop with a mouse and keyboard. The interface stays quiet and neutral so the footage carries the colour. Amber marks what the user acts on, and coral is used only for the playhead.

## Principles

- **Footage first.** Chrome is neutral grey (`bg-000`…`bg-500`). The only saturated colours in the UI are the clip kinds, `accent` and `playhead`. Never tint a panel.
- **One meaning per colour.** `accent` means selected, active or primary. `playhead` means the current time. `focus` means keyboard focus, snapping or a drop target. `danger` means destructive. Do not reuse them for decoration.
- **Hybrid timeline.** V1 is the *main track*. While its magnet is on (`P`), gaps close automatically: deleting ripples, and trimming ripples the clips after it. Every other track places clips freely. Show the mode with the magnet toggle in the track header and the toolbar.
- **Direct manipulation, then precision.** Every drag shows a live result and a timecode readout (`Tooltip`, readout variant). Every drag also has a keyboard equivalent and a typed field in the inspector.
- **Report, never apply.** The element emits a change with before and after states and waits for the app to validate it. Engine limits, such as speed from 0.25× to 4× and the maximum transition length, show up as readouts like "max 2.0 s" and are never discovered silently.

## Content and voice

- Sentence case everywhere: "Split at playhead", "Add effect". Title case is only for proper names.
- Write labels as verbs for actions and nouns for places: "Import media", "Inspector".
- Use the units the engine uses: seconds with one decimal on clips ("4.2 s"), full timecode `HH:MM:SS:FF` in fields and readouts, and the speed multiplier with ×: "1.5×".
- Show the shortcut beside the label in menus and tooltips: "Blade B". Never use emoji.

## Colour

- Use themes `dark` (default, best for judging pictures) and `light`. Set the theme on the window root; components read only tokens.
- Layering from back to front: `bg-000` app ground and gutters → `bg-100` panels → `bg-200` lanes and fields → `bg-300` raised elements (track headers, ruler, menus) → `bg-400` hover → `bg-500` pressed.
- Text: `ink` on everything, including all `clip-*` fills. Use `ink-muted` for secondary text and `ink-faint` only for ruler labels, placeholders and disabled text, and only on `bg-100`…`bg-300`.
- Clip kinds pair a fill with an edge: `clip-video`/`clip-video-edge`, `clip-image`/`clip-image-edge`, `clip-card`/`clip-card-edge`, `clip-title`/`clip-title-edge`, `clip-audio`/`clip-audio-edge`. The edge is the 3 px top stripe and the 1 px border, and it repeats as the 3 px kind strip on the track header.
- Selection is a 2 px `accent` ring around a clip, span or transition, and `accent-soft` behind a selected row or track header.
- The transition badge is inverted (`transition-fill` with `on-transition`), so it reads over any clip.
- Guides are translucent: `beat` for beat marks, `range-fill` for the work area, `trim-overflow` for unused source media, `drop-soft` for drop zones and `muted-hatch` as the scrim on a muted clip.
- Markers use `marker-amber` (the default), `marker-coral`, `marker-green`, `marker-cyan` and `marker-violet`, and always carry a label.
- Contrast: all text pairs reach 4.5:1 and all marks (`focus`, `playhead`, `accent` rings, `line-strong`, clip edges) reach 3:1 on every surface and clip fill, in both themes.

## Type

- Set the interface in IBM Plex Sans (`--font-sans`) and every number that changes over time in IBM Plex Mono (`--font-mono`) with tabular figures, so timecode never jitters. Both are OFL and hosted on Google Fonts. Bundle the files with the app for UltraCanvas.
- Styles: `display` for empty states and dialog titles, `title` for panel titles, `body` as the default, `label` for buttons, tracks and clip names, and `caption` for badges and hints. Use `timecode-lg` for the transport, `timecode` for fields and readouts, and `timecode-sm` for ruler labels.
- Truncate clip and track names with an ellipsis, never wrap them. Hide the duration badge when a clip is narrower than 110 px, and the effect icons below 70 px.

## Space, size and shape

- Use a 4 px grid: `space-1` 4, `space-2` 8, `space-3` 12, `space-4` 16, `space-6` 24, `space-8` 32.
- Fixed geometry comes from the `size` family. Multiply it by the UltraCanvas DPI scale. Controls are `control-h` 28. Default track heights are `track-h-video` 64 (filmstrip visible), `track-h-audio` 48, `track-h-overlay` 28 and `track-h-min` 24 when collapsed. Tracks are resizable by their bottom edge.
- Corners: `radius-sm` for clips, buttons and fields, `radius-md` for panels, menus and thumbnails, `radius-lg` for dialogs and the empty-state drop zone, `radius-xs` for badges and markers, and `radius-pill` for sliders.
- Borders, not shadows. Panels are separated by 1 px `bg-000` gutters. Only floating things cast a shadow: `shadow-menu` for menus and tooltips, `shadow-drag` for a dragged clip.
- Focus ring: 2 px solid `focus`, offset 2 px. It meets 3:1 on every surface.

## Layout of the editor

The reference is 1440 × 860 (see `EditorLayout`). From top to bottom:

1. **App bar** (`appbar-h` 40): project name and format, undo/redo, a save state, and the primary **Export** button on the right. It is the only primary button on screen.
2. **Work area**, in three columns: the media bin (`bin-w` 280, min 220), the viewer (flexible, 16:9 frame on `bg-000` with its transport bar), and the inspector (`inspector-w` 300, min 260). Each side panel can collapse to zero.
3. **Timeline dock**: a horizontal splitter (`splitter-w`), the timeline toolbar (`toolbar-h` 36), then the timeline. It starts at about 35 % of the window height, and the splitter remembers its position.

## The timeline

Structure: a header column (`track-header-w` 200) and the scrollable body. The ruler (`ruler-h` 28) sits on top, and the playhead runs through the ruler and every lane. Track order from top to bottom: overlay/title lanes, free video tracks (V2…), main track V1, then audio (A1 music, A2…). Video stacks upward from V1 and audio downward from A1, so the main track is always where the eye rests.

### Interaction rules

- **Playhead:** click or drag on the ruler. Use `←`/`→` to step one frame, `Shift+←`/`→` one second, `↑`/`↓` to jump to the previous or next cut, and `Home`/`End`. Use `Space`, `J`/`K`/`L` for playback.
- **Zoom:** the wheel zooms around the pointer, and a trackpad pinch does the same. `+`/`−` zoom by keyboard, and `Shift+Z` fits the project. `Shift`+wheel or a horizontal trackpad swipe scrolls. During playback the view pages to follow the playhead.
- **Tools:** `V` Select, `B` Blade, `T` Trim, `Y` Slip. The Select tool already trims at clip edges, and the cursor changes for trim, slip and blade.
- **Edges:** dragging an edge trims it. On the magnetic main track the clips after it ripple; elsewhere a gap is left. `Alt` inverts this. `Ctrl`+drag on an edge retimes (changes speed) within 0.25×–4×. While hovering or trimming, the unused source beyond each edge shows as `trim-overflow` with "+2.3 s".
- **Moving:** the main track reorders and never places clips at free positions. The ghost lifts with `shadow-drag` and `opacity-ghost`, neighbours slide aside in `duration-base`, and an `accent` insertion marker shows where the clip lands. Free tracks move clips to any time.
- **Snapping** (`N`) snaps to cuts, the playhead, markers, the work-area range and beat marks. While snapped, a `focus` line with a diamond on the ruler is shown.
- **Edit keys:** `S` splits at the playhead, `Del` is ripple delete on the main track (or leaves a gap elsewhere), `Shift+Del` inverts that, `Ctrl+D` duplicates, `,`/`.` nudge one frame, `M` adds a marker, `I`/`O` set the work area, and `Ctrl+A` selects all.
- **Selection:** click; `Shift`+click for a range; `Ctrl`+click to toggle; drag on empty lane space for a rubber band (`focus` border, `drop-soft` fill). The selection drives the inspector.
- **Drop:** files from the file manager or the media bin highlight the lane (`drop-soft`, a dashed `focus` outline) and show a `focus` insertion marker at the nearest cut, with the count and time ("Insert 3 files at 00:00:08:00"). Transitions dropped from the bin target a cut.
- **Context menus** use the framework menu. Destructive items go last, in `danger`.

## Iconography

- Icons are drawn on a 16 px grid with a 1.5 px stroke, round caps and joins, no fill, in `currentColor`. They come at `icon-size` 16, or 12 inside clips and menus. The set lives in `UltraVideo.icons` in the bundle as path data, so each icon can become a Cairo path or an SVG for UltraCanvas.
- The current set was drawn for this system as a starting point and is not a finished icon family. Replace it with one consistent open set (for example Lucide, ISC-licensed) before release, keeping the same names.
- Every icon-only button has a tooltip naming its action and shortcut.

## Motion

- `duration-fast` (80 ms) is for hover and press, `duration-base` (160 ms) for neighbours sliding during a reorder and for panels collapsing. The playhead, scrubbing and drag ghosts follow the pointer with no easing.

## Implementing in UltraCanvas

- Map every token to the UltraCanvas theme. Do not hard-code values in `UltraCanvasVideoTimeline`.
- `components/index.d.ts` describes the neutral timeline model (tracks, clips, transitions, markers) that the reference renderer in `components/bundle.js` draws. Treat the renderer as a visual specification, not production code.
- Draw thumbnails, waveforms and the viewer frame from asynchronous providers. Until data arrives, draw the clip fill without a filmstrip, and never show a spinner inside a clip.
