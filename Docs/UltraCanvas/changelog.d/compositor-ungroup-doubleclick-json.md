- **Compositor diagram: Ungroup no longer deletes an ordinary node.**
  `Ungroup(id)` did not check that `id` was a group, so on an ordinary node
  with no children it took the "empty group" branch and removed the node -
  which the demo's Ungroup button did to every such node in the selection.
  It now does nothing for a node whose template is not `isGroup`
  (UltraCanvasCompositorDiagram 0.4.1).
- **Compositor diagram: `onNodeDoubleClick` fires.** It was declared and
  never called; a left double-click on a node (not on a socket) now calls it.
- **Compositor diagram: a saved graph loads back whole.** `ToJson` wrote no
  `parentId` and no link label or colours, and `FromJson` read none, so a
  saved subgraph came back with its children at the top level. Both now use
  the per-node / per-link writers and parsers `Copy` / `Paste` use, which also
  carry the link's line colour and label colours.
  `Tests/DiagramJsonLocaleTest.cpp` saves a grouped graph and loads it back.
- **Compositor demo: Save and Load buttons.** The page's header listed them
  but the toolbar had none. Save writes the active tab's graph as JSON, Load
  reads one into the active tab (dialogs and the read through
  `UltraCanvasFileLoader`, UTF-8 paths); a double-click on a node names it in
  the status line.
