- **The bars' tooltips are no longer covered by other windows.** The bars
  live in the desktop window, which is kept below every other window, and a
  tooltip was drawn inside it: one that rose above the taskbar, or out of the
  right bar, went under whatever application was open there. Tooltips now
  have a window of their own above everything (the framework's
  `WindowType::Tooltip`, see its changelog), so every icon's tooltip is seen
  in full wherever the windows are.
