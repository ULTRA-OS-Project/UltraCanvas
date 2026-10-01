- **Configure warns when a build will have no spell-check backend.** With
  Hunspell absent and no native backend to fall back on (Linux without
  enchant-2, Windows without the spell-check API, Android, WASM), the library
  built with a `STATUS` line and spell checking was silently off: the service
  reported zero dictionaries, named its backend "Hunspell (not compiled in)",
  and `TextAreaSpellCheckTest` skipped. That case is now a CMake `WARNING`
  that names the package to install on each platform and points at
  `Docs/Dependencies.md`. A missing Hunspell beside a working native backend
  stays a `STATUS` line.
