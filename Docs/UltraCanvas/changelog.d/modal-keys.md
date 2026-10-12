- **A key the system delivers to a window a modal dialog blocks reaches the
  dialog.** `UltraCanvasApplicationBase` 1.10.0: a KeyDown, KeyUp, TextInput
  or TextComposition aimed at a blocked window is redirected to the modal
  instead of dropped, so Escape closes the dialog, Return takes its default
  answer and typing reaches its field wherever the keyboard focus happens
  to be - under a focus-follows-pointer window manager, or with none at
  all (an Xvfb session), where the pointer over the main window used to
  swallow every key while a dialog was up. Pointer events aimed at a
  blocked window are still swallowed, and a click there still raises the
  modal.
