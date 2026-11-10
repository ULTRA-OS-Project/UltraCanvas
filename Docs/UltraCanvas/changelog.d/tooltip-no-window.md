- **A tooltip asked for by an element with no window crashed.** An element
  not yet in a window - built but not added, drawn offscreen, driven by a
  test - passes nullptr to `UltraCanvasTooltipManager::UpdateAndShowTooltip`
  when the pointer rests on it, and the manager then started its show timer
  through an application that a program drawing only offscreen does not
  have. With no window the manager now shows nothing and hides a tooltip
  shown for another window; its timers are started and stopped only when
  there is an application. `TooltipNoWindowTest` covers it, and
  `KanbanFileDropTest` runs its hover test with tooltips on again.
