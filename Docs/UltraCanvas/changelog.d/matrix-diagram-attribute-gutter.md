- **Matrix diagram: the attribute gutter (C4).** A row set can now carry a few
  plain-text facts per item - a head count, an owner, a Y/N flag - beside its
  relationships. `MatrixItemSet::columns` names the columns and
  `MatrixItem::attributes` holds each item's values; an `L` or a `House` draws
  them as a gutter of text columns between the row labels and the grid.
  - Each column is as wide as its header or its widest value, up to
    `MatrixDiagramStyle::maxAttributeColumnWidth`. Pointing into the gutter
    selects the whole row, and a row's tooltip lists its values, which is
    where the `T`, `X` and `Y` shapes show them.
  - `SetShowAttributes(false)` hides the gutter and gives the grid its width
    back. The values round-trip through JSON. `Validate()` warns when an item
    has more values than its set has columns.
  - The `ImprovementTools` sample's departments now carry Staff and
    Facilitator columns, and the DemoApp has an **Attribute columns** switch.
