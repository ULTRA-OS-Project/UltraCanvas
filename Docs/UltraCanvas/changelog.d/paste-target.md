- **Knowing what a program takes when something is pasted into it.**
  UltraDesktop's clipboard panel now puts first what the window under
  Super+V takes - images for a paint program, files for a file manager, code
  for an editor - and the pieces it is built from are the framework's:
  - `UCDesktopEntry` reads `Categories=` (`categories`) and
    `StartupWMClass=` (`startupWMClass`).
  - `UltraCanvasDesktopShell::MatchApplication(window, applications)` finds
    a window's desktop entry: its `StartupWMClass` first, then the program,
    icon or name its `WM_CLASS` spells, case aside (`Gimp-2.10` is
    `gimp-2.10`'s).
  - `PreferredClipboardKinds(categories, mimeTypes)` turns an entry into the
    kinds of clipboard entry it takes, most wanted first; empty when nothing
    says.
  - `ClipboardHistoryQuery::newestFirst` lists by last use, pins aside -
    "the last image copied", which UltraPaint's Paste now offers when the
    clipboard holds no picture.
  - `ClipboardHistoryListModel::SetEntries` takes a `LeadSection`: the first
    entries under a title of the caller's, above the usual sections.
