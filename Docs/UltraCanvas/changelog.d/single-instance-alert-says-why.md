- **The "already running" alert says why the running copy did not come
  forward, and speaks the application's language.** A second start that
  cannot hand over to the running copy says "<App> is already running" and
  asks the person to switch to its window. Why the window had not simply come
  was only in the debug log - so with a bus that refused its folder, or a
  copy that hung, the alert sent the person looking for a window with nothing
  to go on. `UltraCanvasSingleInstance::RunAlreadyRunningAlert` now adds the
  reason `Claim()` found:
  - the bus could not be joined: "It could not be asked to come forward:"
    and what UltraMessage said (`the bus folder ... is not this user's
    alone: ...`, `bus path too long for a Unix socket: ...`), which is in
    the person's language too (the entry on UltraMessage's reasons);
  - the copy is on the bus and did not answer within the fifteen seconds a
    start waits: "It did not answer when asked to come forward."
  The debug log has the same line at the end of the wait. A build without
  UltraMessage, where the lock alone decides, adds nothing.
  - The alert's texts went straight to the screen in English in every
    language. They go through `Tr` now, and the four are in the standard set
    (`media/locale/en/Standard.json`) with drafts in all 29 languages
    (`machine draft ..., reviewed by -`, as the rest of each file).
  - No caller changes: the thirty applications call the alert as before.
