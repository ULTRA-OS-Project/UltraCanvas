- **`UltraCanvasModalDialog` 3.7.0 follows the theme.** `ModalDialogStyle::FromTheme`
  puts the theme's ink on the message and muted ink on the details over a
  style's own sizes, and the dialog's `ApplyTheme` colours its surface with
  the panel role and its footer buttons by their role (the default answer
  `ButtonVariant::Primary`, the others `Standard`; a destructive one keeps
  its red). `SetFollowTheme(true)` after the buttons are added applies it
  and keeps the dialog with the theme while it is open; the elements a
  host added follow on their own. UltraVideo's export, unsaved-changes and
  story dialogs follow the theme and take no palette.
