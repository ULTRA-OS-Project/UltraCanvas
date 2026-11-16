- **Radial cluster map (Phase 1).** `UltraCanvasMindMap` gains the `Balloon`
  structure: every topic carries its children on a ring around itself, so a
  large tree grows as clusters of clusters. Fans opening away from the parent
  (`balloonFanSweep`) or full rings (`MindMapBalloonRing`); ring radii from
  the wedges the children's subtrees cover, so no two nodes of the map
  overlap; main topics pushed out by `balloonTrunkLength`; a topic below the
  centre can be laid out in another structure, a one-sided one mirrored to
  grow away from the centre. New marks: the `Ring` connector (a thin ring
  through a hub's children, an arc for a fan), `MindMapStyle::sizeByDepth`
  and `connectorWidthByDepth`, and the `Triangle` node shape (also in the SVG
  export). Labels in circles and triangles are centred, and a circle's label
  no longer breaks a word. `ToJson` / `FromJson` carry the new options and
  `depthLightenStep`. Demo: **Diagrams > Mind Map > Radial cluster**, an
  invented family emergency plan of about 150 topics, and `Balloon` in the
  gallery tab. Tests: `MindMapLayoutTest` (balloon geometry, overlaps,
  determinism, overrides), the new `MindMapRenderTest`. Guide:
  `UltraCanvasMindMapExamples.md`; design:
  `UltraCanvasRadialClusterMapProposal.md`.
