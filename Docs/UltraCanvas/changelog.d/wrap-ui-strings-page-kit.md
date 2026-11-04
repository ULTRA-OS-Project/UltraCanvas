- **`scripts/wrap_ui_strings.py` 1.1.0 knows the settings page kit.** The
  migration tool wraps the text of `MakePage` (title and caption),
  `MakeButton`, `MakeText`, `MakeLabel`, `MakeChoice`, `MakeCheckbox`,
  `MakeLabelledRow`, `MakeSliderRow`, `AddNote` and `AddTreeNode`, which
  carry it after the id, and a dropdown's `AddItem`, `AddCustomButton`,
  `AddFilter`, `SetFilterEmptyAction`, `SetOpenPathMenuItemVisible` and
  `MakeFolderNodeData`; a literal continued over several lines is wrapped
  as one `Tr(...)`. On UltraFiler's settings window before its pass it
  finds 178 strings where it found 6.
