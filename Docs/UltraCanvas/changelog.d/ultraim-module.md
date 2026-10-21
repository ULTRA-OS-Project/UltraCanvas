- **UltraIM: an instant-messaging framework, the base of the ChatCube
  messenger.** A new UI-free module (`UltraCanvas/{include,core}/UltraIM/`,
  target `UltraIM`) with one `UltraIM_*` API for every messenger network:
  providers implement `IUltraIMProviderPlugin` and ship as modules loaded by
  `UltraIM_RefreshProviders` (checked against
  `ULTRAIM_PLUGIN_HOST_ABI_VERSION`) or are registered in-process; every
  event and callback reaches the application through the dispatcher it
  installs, so a UI subscribes on its UI thread and needs no lock; a
  conversation fetch merges every connected provider; sends carry a client
  message id for optimistic display. Plan:
  `Docs/Research/ChatCubeDirectPlan.md`; doc: `Docs/Modules/UltraIM/README.md`;
  tests: `Tests/UltraIM` (`ULTRACANVAS_BUILD_ULTRAIM_TESTS`).
