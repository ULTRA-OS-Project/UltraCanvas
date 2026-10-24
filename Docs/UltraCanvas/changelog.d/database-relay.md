- **UltraDatabase: connections through a relay** (`UltraDatabaseRelay.h`).
  The `relay` driver sends every call of a connection - queries, scripts,
  cursors, cancelling, introspection, server administration - to a relay on
  the database's side, as JSON over a transport the application gives
  (`UltraDb_SetRelayConnector`), so a program that cannot reach a database
  itself, such as a page in a browser, works against UltraDatabase
  unchanged. `UltraDb_RelayConnections` asks a relay what a token may open.
  `UltraDbRelayServer` is the other side: it offers connections by name
  only, knows its users by the SHA-256 of their tokens with read or write
  access to each connection (read is the driver's read-only mode), caps
  rows, requests and sessions, and reports every statement with the user
  and address that ran it. `UltraDatabaseRelayUltraNet.h`, header-only, is a
  transport over UltraNet's WebSocket.
- **UltraDatabase: closing a connection no longer holds the registry lock
  while the driver closes it.** A driver whose close waits on another
  connection - a relay in the same process - deadlocked.
- **WebAssembly: UltraDatabase with SQLite in a browser build.** With
  `BUILD_ULTRADBADMIN`, an Emscripten build links Emscripten's sqlite3 port
  into UltraDatabase.
- **The WebAssembly workflow also builds UltraDBAdmin's browser page** (the
  baseline variant) and adds it to the uploaded pages.
