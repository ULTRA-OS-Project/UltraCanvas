- **The desktop can be translated, and is, in thirty languages.** Six in
  seven of its texts were written straight into the code - every tooltip of
  the bars, the menus of the clock, battery, info bar, clipboard and
  settings button, the Wi-Fi, Bluetooth, loudspeaker, clock and clipboard
  panels, the Task Manager, the Apps window with its categories and
  sub-kinds, the sticker board, the dialogs - so the desktop showed in
  English whatever the language. They go through `Tr` / `TrF` / `TrN` now
  (415 texts where the catalogue had 61), sentences that were glued from
  fragments are whole texts with placeholders, and counted texts have their
  plural forms.
  - Label tables that kept their texts beside ids or icons carry `Tr`
    literals where they are used, so the extractor sees them: the clock
    styles, the taskbar edges (whose `Tr(choice.label)` reached no
    catalogue), the day and month names, the board designs.
  - The Apps window's categories and sub-kinds keep their English names for
    sorting and tests; `AppCategoryDisplayName`, `AppSubcategoryDisplayName`
    and `AppGroupDisplayName` (`UltraDesktopAppCategories.h`) give the
    names it shows. `StartupProgramLabel`, `StartupProgramName` and
    `InfoItemLabel` (`UltraDesktopSettings.h`) do the same for the programs
    that start with the desktop and the info bar's icons.
  - The short date on the clock takes a short month name of its own
    instead of the first three bytes of the long one, which cut a
    translated name in the middle of a letter.
  - The task list recognises the clipboard panel's window by its translated
    title too.
