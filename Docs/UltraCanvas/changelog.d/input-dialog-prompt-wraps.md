- **An input dialog shows its whole prompt.**
  `UltraCanvasDialogManager::ShowInputDialog()` opened a window of a fixed
  400 x 150 and put the prompt in a one-line label above the field, so a
  prompt longer than the window was wide ended in an ellipsis - UltraAI's
  *Add from Hugging Face* read "The repository and the GGUF file in it, as
  "organis…" - and the label's width squeezed the severity icon to a sliver
  beside it. The prompt is the message area's now: it wraps at the dialog's
  width and the window grows to the height the lines and the field need, up
  to most of the monitor, past which the prompt scrolls and the field and
  the buttons stay. A short prompt leaves the dialog at its configured
  size. A config that carries a `message` as well keeps `inputLabel` as the
  field's caption, and that wraps too. `Tests/InputDialogPromptTest.cpp`
  pins it on a real window. Every caller gains it: the password prompts of
  the file loader and the viewers, the rename dialogs, UltraDBAdmin's and
  UltraPassword's prompts.
