- **Startup and frame timings, for an application that starts slowly or
  draws slowly.** An application could time `Initialize()` only as a whole,
  and a frame not at all - UltraMail's window stays black for ten seconds and
  more on Windows, and which of the framework's steps, or the first frame,
  takes that time could not be told from outside.
  - `UltraCanvasApplicationBase::GetStartupTimings()` lists each step of
    `Initialize()` with its time (`StartupStageTiming`): fontconfig setup,
    the image subsystem, the native backend, the bundled and system fonts,
    the clipboard and the default window icon. The same lines go to
    `debugOutput` ("UltraCanvas: startup step ... took N ms").
  - `UltraCanvasWindowBase::onFrameRendered` is called after every frame that
    laid out or painted anything, with a `WindowFrameTiming`: the time of the
    layout, of painting the dirty rectangles and popups, and of compositing,
    and how many rectangles were painted. Nothing is timed while it is unset.
  - Doc: `Docs/UltraCanvas/UltraCanvasWindowsDiagnostics.md`, "Slow to start,
    or a window that stays black". UltraMail 0.10.35 prints both in its
    timing trace.
