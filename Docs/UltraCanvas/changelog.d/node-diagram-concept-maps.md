- **`UltraCanvasNodeDiagram` 2.5.0 draws concept maps.** Four things a
  hand-drawn map of concepts needs and the element did not have:
  - **Free-placed regions.** A `NodeDiagramGroup` with `freeBounds` set draws
    at its own `bounds` rectangle instead of around its members.
    `AddRegion(id, label, bounds, fill)` makes one; `SetGroupBounds()` /
    `ClearGroupBounds()` switch a group between the two. A region stays where
    it is put - the pass that moves cluster boxes apart skips it and no node
    is pushed out of it - so regions overlap each other, cluster boxes and
    nodes, which a member-fitted box never could over empty space.
  - **`NodeShape::Ellipse`**, filling the node's width and height (`Circle`
    remains a circle of the smaller side). Hit testing, the shadow and the
    label fit follow the ellipse, and like `Rectangle` it grows for its label
    in width and height separately.
  - **Node annotations.** `NodeDiagramNode::annotation` is a paragraph drawn
    beside the node (`annotationPosition`: right, bottom, left, top), wrapped
    at `annotationMaxWidth`, line breaks kept; `SetNodeAnnotation()` and
    `GetNodeAnnotationBounds()`. `FitView()` and the minimap count it; the
    overlap passes do not.
  - **Path highlighting.** `HighlightPath(nodeIds, color, width)` marks the
    link between each consecutive pair of nodes, in either direction, and
    returns how many it marked; `HighlightLink()`, `ClearLinkHighlight()`,
    `ClearHighlights()`, `GetHighlightedLinkIds()`. A highlighted link draws
    above the others and keeps its own colour and width for when the
    highlight is cleared.

  All four are written by `ToJson()` and read by `FromJson()`. The demo's
  Node Diagram page has a **Concept map** tab that uses them together, and
  `Tests/NodeDiagramConceptMapTest.cpp` pins them.
- **`UltraCanvasNodeDiagram::FromJson()` unescapes strings.** It returned a
  string as it stood in the file, so a label with a quote, a backslash or a
  line break came back with the escape still in it, and a string ending in a
  backslash ran on into the next key.
