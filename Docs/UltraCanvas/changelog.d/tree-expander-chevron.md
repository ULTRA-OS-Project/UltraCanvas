- **Tree view: the expand button can be an arrow.** `SetExpanderStyle`
  (`TreeExpanderStyle::Chevron`) draws a row's expand button as an arrow -
  pointing right when the row is closed, down when it is open - in
  `SetExpanderArrowColor`, instead of the `+` / `-` in a grey square that
  every tree had. It takes the same place and the same click, so a tree
  switches without any row moving; `TreeExpanderStyle::Box` stays the
  default. The demo app's TreeView page shows it on the multi-selection
  tree, with a switch back to the box and its dotted lines; UltraDBAdmin's
  connection tree is the first application to use it.
