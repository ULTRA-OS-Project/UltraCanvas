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

  - **Titles outside the box.** `GroupLabelPosition` gains `BottomRight` and
    ten `Outside...` values: above or below the box (left, centre, right)
    and beside it (level with its top or bottom edge). An outside title
    takes no room from the box, and `FitView()` counts it.
  - **Link waypoints.** `NodeDiagramLink::waypoints` are world points a link
    passes through - a smooth curve through them for `LinkStyle::Bezier`,
    straight runs otherwise - so an edge can loop round the outside of a
    map. `SetLinkWaypoints()`, `AddLinkWaypoint()`, `ClearLinkWaypoints()`.

  All of it is written by `ToJson()` and read by `FromJson()`. The demo's
  Node Diagram page has a **Concept map** tab that uses them together, and
  `Tests/NodeDiagramConceptMapTest.cpp` pins them.
- **`UltraCanvasNodeDiagram::FromJson()` unescapes strings.** It returned a
  string as it stood in the file, so a label with a quote, a backslash or a
  line break came back with the escape still in it, and a string ending in a
  backslash ran on into the next key.
- **A node diagram's links end at the node's outline.** A link that ends on
  a node (not on a handle) ran on to the node's centre, under the node, so
  the arrowhead of every directed link was hidden and the link showed
  through a translucent node. It stops where it meets the shape now - an
  ellipse, a star's points, a cloud's lobes - with the arrowhead on the
  outline; a thick link gets a head to match. The link's label sits halfway
  along the line as drawn, and a click is tested against that line, curves
  and steps included, where it was tested against the straight line between
  the two centres. Diagrams with directed links between plain nodes show
  their arrows for the first time.
- **`NodeShape::Cloud` is a cloud.** It drew three small separate ellipses
  offset to the lower right of the node - their rectangles were built as if
  `FillEllipse` took a centre. It is one shape filling the node, with one
  outline; clicks and link ends follow its lobes. Star and Hexagon nodes are
  hit by their own outline too, not by a circle or an ellipse around them.
