- **Performance matrix, Phase 2: the population.** People can now be placed
  into the boxes: `PerformanceMatrixModel::items`, each in an explicit cell or
  placed by two scores mapped through the axes' level bounds (an explicit cell
  wins, as a calibration override does). There are counts per cell, row and
  column, groups, and an optional `targetDistribution`.
  - `SetPopulationDisplay` shows each cell's people as a count chip (the
    default), a share against the target ("14% / 10%"), dots in the group
    colours, or names in columns. "+N" / "+N more" stand in for whoever does
    not fit, and every person is either drawn or counted.
  - A group legend at the right shows each department's size. A click hides
    a group from the cells, the counts, the shares and the colouring.
  - `SetAnonymised(true)` turns names into counts and stops any person
    answering the pointer, for a room that should not see names.
  - New `PerformanceCellColoring::ByCount` colouring, and level colour bars
    along both axes (`SetShowLevelBars`). `SetShowTotals` adds "n = 12"
    under each level label.
  - A person's tooltip gives the name, group, box, levels, scores and note,
    and `onItemClick` reports a click on a dot or a name.

  The model gains `LevelForScore`, `CellOf`, `CountsPerCell` / `Row` /
  `Column`, `Groups`, item and target validation, and the invented
  `SampleTeam()` and `SampleTargetDistribution()`. The demo's Performance
  Matrix page has a Calibration variant with a People control. Model and
  render tests cover placement, the "+N" accounting, hiding a group and the
  anonymised mode.
