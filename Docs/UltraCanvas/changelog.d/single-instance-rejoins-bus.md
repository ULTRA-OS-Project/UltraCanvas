- **A running copy no longer goes deaf when the message bus's host ends.**
  `UltraCanvasSingleInstance` hands a second start to the running copy over
  UltraMessage, and the bus lives in whichever program connected first. When
  that program ended - the desktop restarting, an application that happened
  to host it closing - the running copy's endpoint lost the bus and never
  reconnected, so every later start waited fifteen seconds and said "*App* is
  already running" instead of bringing it forward: clicking a pinned UltraSnip
  that the desktop had preloaded, for one. A listening copy now checks its
  connection once a second and joins again (hosting the new bus when nobody
  does); a start made meanwhile keeps asking and is handed over.
