#### 2026-11-22 *0.1.0*
- **UOS-Localisation, the ULTRA OS translation editor.** The localisation
  editor leaves UOS-Settings, where it was the *Language > Translations*
  page, for a window of its own: every set an application's texts come
  from - its own set, the base sets it takes, the standard set - in one
  grid, for any application and any language.
- **Laid out for a whole window.** The choices on two rows across the
  width instead of four over a narrow page; the grid beside a panel that
  shows the selected row whole - its key, its English, the comparison
  text, what the application shows and where it comes from, every set's
  text - wrapped rather than cut to a column, with the edit at its foot
  (*Apply*, *Clear override*). It is the framework editor's new Window
  arrangement (`LocalisationEditorLayout::Window`).
- **`--app` and `--lang`** open an application in a language; one copy per
  user, so a second start brings the first forward on what it names -
  unless that would drop unsaved edits. Closing the window with unsaved
  edits asks first.
- It opens in the language chosen in UOS-Settings (*Language*), and writes
  the *Translator* name kept there into the catalogues it saves.
