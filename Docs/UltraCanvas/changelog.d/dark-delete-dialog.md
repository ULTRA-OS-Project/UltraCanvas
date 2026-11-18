- **The delete, copy and move questions of the file display are dark under a
  dark theme.** Their list of the entries concerned had white rows and a light
  grey header in a dark dialog: setting its row and header sizes by hand had
  stopped the list following the theme. It keeps its sizes and follows the
  theme again (`ListView` well, header and ink).
- **A dialog's red answer beside the default one reads in a dark dialog.**
  `DialogButtonRole::Destructive` ("Delete permanently" beside "Move to
  Trash") was a fixed light pink button with dark red text, which glared from
  a dark dialog. It is now the theme's ordinary button, outlined and lettered
  in the theme's `danger` colour (`UltraCanvasModalDialog::DestructiveOutlineStyle`):
  a dark red on a light button in a light look, a light red on a dark one in a
  dark look, filled red while pressed, and it changes with the theme. A
  dialog whose host set its fonts by hand before adding the answers (the file
  display's) gets it too, wherever the application has its elements follow
  the theme. `UltraCanvasThemeTest` checks both looks and the list.
