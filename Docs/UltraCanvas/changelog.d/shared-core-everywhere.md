- **One shared core on every platform, with the modules inside it.** The
  framework is now `libUltraCanvas.dylib` on macOS as it has been `.so` on
  Linux and `.dll` on Windows: `ULTRACANVAS_BUILD_SHARED` defaults ON on every
  desktop platform and every CI row passes it explicitly (the Windows row had
  relied on the LaTeX plug-in forcing it). Until now each of the ~20 macOS
  `.app` executables carried the whole framework statically, so the suite
  shipped and loaded the same code twenty times over; `package-macos.sh`
  bundles the core into the suite's shared `Frameworks/` like any other dylib,
  `verify_suite` fails a suite without it, and the `ultramsg` tool loads from
  that shared folder too instead of carrying a second copy.
- **The UI-free modules are folded into the shared core whole.** UltraCrypt,
  UltraVault, UltraDatabase, UltraMessage, NetworkMonitor and VirtualFS join
  UltraNet and UltraWin as `$<LINK_LIBRARY:WHOLE_ARCHIVE,…>` members of the
  core, so the DSO exports each module's complete API and one copy of its code
  and global state serves every running application. Before, an app that
  linked `UltraDatabase` or `UltraVault` next to the shared core (UltraMail,
  UltraSocial, UltraFIBU, the test suites) got the archive ahead of
  `libUltraCanvas` on its link line and took the module's objects from there
  while the core carried its own - two registries, two connection tables, and
  on Windows the "multiple definition" errors the changelog has recorded
  before. The public target names (`UltraDatabase`, `UltraVault`, `UltraCrypt`,
  `UltraMessage`, `NetworkMonitor`) are now INTERFACE "homes" that resolve to
  the core on a shared build and to the archive (`uc-database`, `uc-vault`,
  …) on a static one, so no consumer changed; "MODULE HOMES" in
  `UltraCanvas/CMakeLists.txt` and *One shared core* in `AGENTS.md` have the
  rules.
- **CI reports the package sizes.** The Linux and Windows rows write a
  "Package sizes" table to the job summary - the package, the core library,
  the sum of the application executables and each one - next to the macOS
  suite table, which now lists `libUltraCanvas.1.dylib` on its own line, so
  a change that puts a second copy of the framework into an app shows up on
  the run page.
