The complete `UltraCanvasVideoTimeline` element: a header column and a scrolling body with the ruler, lanes, clips, transitions, beat marks, markers and the playhead.

**Consumer provides:** a model (`TimelineModel` in `index.d.ts`), providers for thumbnails, peaks and frames, and handlers for change and validation callbacks. The element never plays media; the app links the player's position to `playhead`.

- Lane order: overlay lanes, free video tracks, V1 main track, then audio. Default heights come from `track-h-*`.
- Draw from back to front: lane fill, `range-fill`, `beat`, `trim-overflow`, clips, transitions, insertion and snap lines, playhead.
- Do keep the header column fixed while the body scrolls. Don't scroll the ruler separately from the lanes.
- Don't draw a clip narrower than 6 px. Below that, show a 2 px tick in the clip edge colour.
