- **SmartHome: Matter devices survive a restart, and never get a reused
  node id.** The controller keeps the nodes it commissioned - id and device
  types - next to the SDK's fabric storage (`MatterNodeStore`), registers
  them at start-up and marks each reachable once a session to it opens.
  Node ids come from that list and are recorded before use; every run used
  to start again at 0x100. `tests/MatterNodeStoreTest.cpp`.
- **SmartHome: Matter notifications no longer run on connectedhomeip's
  thread.** A listener that heard a device join and then asked the device
  anything deadlocked, since every synchronous SDK call waits for that
  thread. Reports go through a delivery thread of the backend's own, and a
  synchronous call still made on the SDK thread is refused and logged
  instead of hanging.
- **SmartHome: backend logs reach `debugOutput`.** `Log` and `ReportError`
  in `SmartHomeProtocolBase` discarded everything; they write
  `SmartHome[<backend>] <level>: ...` lines now (on with
  `ULTRACANVAS_DEBUG_LOG`).
- **SmartHome: updates from backends without device objects reach the
  registry.** The manager re-reads the backend's paired devices (Matter,
  Zigbee), so a device coming back online is shown online; Matter devices
  also report `Reachable` at all now, which they never did.
