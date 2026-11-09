- **The bars' menus are no longer covered by other windows.** Every context
  menu of the bars - the running and pinned applications, the clock, the
  screenshot and clipboard buttons, the tray icons - was drawn inside the
  desktop window, which is kept below every other window, so the part of a
  menu beside a bar went under whatever application was open there. Menus,
  and every other popup, now have a window of their own above everything
  (the framework's `WindowType::Popup`, see its changelog).
