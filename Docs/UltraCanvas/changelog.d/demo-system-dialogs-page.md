- **DemoApp: new *ULTRA OS modules → System dialogs* page.** Every dialog an
  application asks the system for, behind a button: File Open, Open multiple,
  File Save and Select folder (`UltraCanvasFileLoader`), Print settings and
  Print test page (`UltraCanvasNativeDialogs::RequestPrintSettings`,
  `PrintTextWithDialog`), and the information / question / warning / error /
  text / password dialogs (`UltraCanvasDialogManager`). A *Dialog style*
  switch shows each one as the ULTRA OS dialog or as the host platform's,
  without changing the demo's own setting, and every answer - paths, print
  settings, button pressed - is written to a log on the page. The *Details*
  tab is the new `Docs/UltraCanvas/UltraCanvasSystemDialogs.md`, which the
  element catalogue now links from its file-dialog rows.
