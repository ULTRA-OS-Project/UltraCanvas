- **DemoApp: the domain dashboard is back, on the list view.** The old
  "Templates demo" page - a domain table with links, an "Enable" action,
  sparklines, visitor figures, plans and a ⋮ menu per row - was taken out of
  the build in January and its file deleted today, because it no longer
  compiled. It built eight elements and a line chart per row in a container.
  The new *Domain Dashboard* page (Extended Functionality, after List View)
  shows the same table with `UltraCanvasListView`: one custom `IItemDelegate`
  paints every cell, `onCellClicked` / `onCellHovered` make the domain a link
  (it opens in the browser through `OpenURL`, where the old page ran
  `system("xdg-open " + url)`) and "Enable" an action, ⋮ and a right-click
  open the row's menu (open, (de)activate, change plan, remove - each edits
  the model), and a header click sorts by that column (the trend column by
  growth). "Add 1,000 domains" shows that only the rows on screen are
  painted. `UltraCanvasListViewExamples.md` explains the pattern under "A
  custom delegate with clickable cells".
