- **Copy a code with one click, and it leaves the clipboard by itself.** A
  copy button right after each card's code puts it on the clipboard; the
  button shows a tick for a moment and the status line says how long the
  code stays valid. The app clears the clipboard again 30 seconds later
  (one time step) and at once when it locks or closes - each time only if
  the clipboard still holds that code, so anything copied since is left
  alone. The copy is marked secret (`ClipboardHint::Secret`: KDE's
  password-manager hint, Windows' clipboard-history exclusion, macOS'
  concealed type), so clipboard histories that honour the marker never
  record it.
  - With *Hide codes* on, the copy works without showing the code.
  - A counter-based (HOTP) card copies the code *Next code* last made and
    never spends a new one; before the first *Next code* it says so instead.
  - The setup key is still never offered for copying.
