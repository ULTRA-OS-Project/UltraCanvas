- **The info bar's icons and the programs that start with the desktop are
  named in the reader's language.** Their names come from the desktop's
  own source, so UOS-Settings now takes the desktop's catalogue as a second
  set (`UltraLocalisation::AddSet("UltraDesktop")`); the info bar's
  checkboxes passed a variable to `Tr`, which no catalogue ever held.
