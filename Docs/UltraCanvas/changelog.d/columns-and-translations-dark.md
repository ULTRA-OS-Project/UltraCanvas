- **Column tree views are readable in the dark look.** A column's cells
  were drawn in a fixed dark grey (`TreeViewColumn::textColor`, 40,40,40),
  near-invisible on a dark ground. The default is now `Colors::Transparent`,
  which means the tree's own text colour, the theme's ink. A column given a
  colour of its own keeps it. UOS-Settings' Translations grid showed it
  most, in its set columns and the Shown column.
- **The localisation editor's marks follow the look.** Its marks are a text
  from another set, an untranslated row and a changed row, plus the status
  line. They keep their colours in a light look. In a dark one they take the
  theme's muted ink, its danger colour and a light green, and a theme can
  name its own (`localisation-editor.muted`, `.untranslated`, `.changed`,
  `.status`). A change of look while the editor is open fills the rows
  again (`UltraCanvasLocalisationEditor::ApplyTheme`).
