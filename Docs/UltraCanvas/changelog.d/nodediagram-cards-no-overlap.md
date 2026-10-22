- **UltraCanvasNodeDiagram 2.3.0: card nodes, and no overlapping nodes.**
  - A node can carry an icon (`iconPath`, drawn in a disc of
    `iconBackground` at its leading edge), a second, smaller line
    (`sublabel`), rounded corners (`cornerRadius`) and a drop shadow
    (`shadow`). With an icon the label sits beside it, left-aligned; a label
    that does not fit ends in an ellipsis instead of running out of the
    node. The fields round-trip through `ToJson` / `FromJson`.
  - `ResolveOverlaps(margin)` moves overlapping nodes apart by their
    rectangles until none overlap; should its passes not settle, it places
    the nodes one by one at the nearest free spot. Pinned nodes stay put. A host that places
    nodes itself calls it before `FitView()`.
  - The force-directed layout ends with it. Its old overlap pass treated a
    wide node as a circle of its half-width, stopped after eight passes and
    clamped the nodes back into the element's bounds - so wide nodes in a
    small element came out on top of each other. It no longer clamps; the
    view fits the content.
  - `SetChargeStrength()` sets the force layout's repulsion.
  - `SetDetailZoom(zoom)`: below that zoom, nodes with an icon draw as their
    icon disc alone - a name too small to read is left out - and show their
    name in a pill while hovered or selected (`IsShowingIconsOnly()`).
  - `NodeDiagramOverlapTest` covers the overlap removal (and its fallback), the force layout
    with thirty wide nodes in a 300 x 200 element, and the JSON fields.
