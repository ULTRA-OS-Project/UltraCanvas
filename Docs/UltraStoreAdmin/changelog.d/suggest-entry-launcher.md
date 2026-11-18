- **The suggested entry of a Linux package is its launcher.** For an archive
  laid out like the suite package - `<dir>/<name>`, the script that puts
  `<dir>/lib/` on the library path, beside `<dir>/bin/<name>` - `package
  add` and `package entries` suggested `bin/<name>`, the executable, which
  started alone does not find `lib/`: the store then installed an app that
  did not start. `SuggestEntry` now takes `<dir>/<name>` when
  `<dir>/bin/<name>` is in the same archive, before the bin/ rule.
