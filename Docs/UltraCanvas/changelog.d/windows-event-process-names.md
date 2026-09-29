- **Windows: the kernel network ETW source names a process it cannot
  open.** It reported `pid 4720` for every process that refused
  `OpenProcess`, while the socket-table backend already named the same
  process from the Toolhelp process list. The list is now one table
  (`UltraCanvasWindowsProcessNames.h`, internal to `OS/MSWindows`): the
  backend refreshes it with every snapshot, and the event source reads it
  for each event, refreshing on a miss at most every two seconds, so a
  connect event from the antivirus proxy reads `AvastSvc` like its row.
  The registry completes the rest: an event whose source knew only the PID
  (an empty executable path) takes the socket table's identity for that
  PID - name, path and user - when the table has the socket.
