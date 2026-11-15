- **UltraNet links without the core again.** `UltraNetMime` calls `Trim()` and
  `Base64Encode()`, which live in the `UltraCanvasTextUtils` archive, but
  `uc-net` never named that archive: the shared core links it and absorbs
  `uc-net` whole, so anything built against the core saw no gap. A program
  that links UltraNet *without* the core did - `ultradbadmin-relay`, the
  headless server half of UltraDBAdmin's browser build, failed to link with
  *"undefined reference to `UltraCanvas::Trim`"* as soon as its OpenID Connect
  sign-in was enabled. `uc-net` now links the archive itself.
