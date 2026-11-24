- **A tooltip asked of a window that is closing no longer crashes the
  program.** A pointer event of a window being closed - queued before it
  closed, as UltraDesktop's window-preview strip closes when the pointer
  leaves it - asked the tooltip manager for a tooltip after the window's
  render context was gone, and `CalculateTooltipLayout` called
  `CreateTextLayout` on a null context (UltraDesktop on Windows: an access
  violation writing address 0x8). A window that is not created is no
  tooltip target now, `UpdateAndShowTooltipImmediately` no longer marks such
  a tooltip shown, the layout stops without a render context, and a text
  layout that could not be made measures 0 (the drawing skipped those
  already). `TooltipOwnWindowTest` asks for a tooltip on a closed window:
  it crashed before, it shows nothing now.
