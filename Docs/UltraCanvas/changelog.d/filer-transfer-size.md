- **UltraFiler: the move / copy question shows how much is going.** The
  *Move files* / *Copy files* confirmation said **Size: 0 B** for every file:
  `UltraCanvasFilerWidget::EntriesForPaths()` built its entries from the file
  status alone and never read a size, so the delete confirmation's totals were
  short by the same amount. It now reads each file's size. A folder is counted
  too, on the background worker the info bar's folder sizes use: the dialog
  opens at once with **Size:** *counting…* and the line fills in when the walk
  lands - "4.5 MB (120 files in 8 folders)" for one folder, "1 folder and 2
  files (4.6 MB)" for a set, "≥" when the walk stopped at its safety cap. A
  folder rescan while the question is open queues the walk again. New
  `OperationDialogSpec::liveFacts` lets any operation dialog re-derive its
  facts as walks land.
