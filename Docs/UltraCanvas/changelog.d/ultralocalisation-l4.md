- **UltraLocalisation phase L4: the localisation editor.**
  `UltraCanvasLocalisationEditor` (`UltraCanvasLocalisationEditor.h`) shows
  every set an application's texts come from - its own set, the base sets
  it takes, the standard set - in one grid, one row per key and a column
  per set, with where each text comes from and its status (application,
  override, duplicate, base, collision between bases, standard,
  untranslated; plural, draft, changed) in the row; a compare column; a
  search and a status filter; the whole shared sets on request. The sets
  are merged in the editor: take or drop a base set from the Sets picker,
  promote a row into a base or demote it to the application in every
  language, merge two bases with the collisions named; every operation
  undoable. A save goes into the source tree where it is writable, in the
  extraction's format with the translator's name, else into the settings
  folder with only what differs from the shipped file; Export writes the
  merged set as a catalogue or as gettext PO. A drafter the host wires to
  UltraAI fills the untranslated rows. `LocalisationWorkspace` is the model
  without UI; `ShowLocalisationEditorDialog` opens the editor on the
  running application; UOS-Settings 0.14.0 hosts it under Language >
  Translations, DemoApp as a page. `LocalisationEditorTest`;
  `Docs/UltraCanvas/UltraCanvasLocalisationEditor.md`.
