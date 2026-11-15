- The status bar's text and the search bar's match counter have their
  identifiers (`StatusBar`, `CountLabel`). Both were built with the label
  constructor that takes one string, which is the label's text and not its
  name, so they had none and briefly held their own name as text until the
  next line replaced it. Nothing on screen changes. The framework now
  refuses that form (`scripts/check_label_identifiers.py`).
