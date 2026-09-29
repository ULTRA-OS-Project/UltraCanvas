- **Every application's signal handler calls
  `UltraCanvasApplicationBase::RequestExitFromSignal()`.** ArtCreator,
  DeviceExplorer, Texter, UltraAI, UltraAuthenticator, UltraCleaner and the
  demo application still called `RequestExit()` (which logs and runs a
  callback) and `std::exit` from the handler, running the static
  destructors under live threads. Each handler is now the one call, and
  the main loop turns it into an orderly exit: `Run` returns and `main`
  shuts down as on a closed window. The `g_app` globals the handlers
  needed are gone.
