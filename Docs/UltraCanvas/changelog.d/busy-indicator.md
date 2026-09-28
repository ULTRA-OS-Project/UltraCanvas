- **New element: `UltraCanvasBusyIndicator`**, the turning ring that says
  *working on it* when there is no percentage to show (a network call, a
  sync, a scan). `CreateBusyIndicator(id, x, y, size)` makes one, and
  `Start()` / `Stop()` / `SetRunning(bool)` control it. By default a stopped
  indicator draws nothing and runs no timer, so it can stay in a status line
  permanently. The angle comes from elapsed time, so a late timer tick never
  slows the ring. `BusyIndicatorStyle` sets the arc and track colours, the
  thickness, the arc length, the speed and the frame interval. Docs:
  `Docs/UltraCanvas/UltraCanvasBusyIndicator.md`; it is listed in the element
  catalogue. UltraMail's status line is the first user.
