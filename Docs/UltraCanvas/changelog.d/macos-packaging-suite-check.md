- **macOS packaging no longer fails every run with "no app bundle was
  produced".** The check that ends `package-macos.sh`, added to fail a run
  that packaged nothing, looked for `*.app` directly in `dist-macos/`, but
  since the apps became one suite folder they are built in
  `dist-macos/UltraCanvas/`. It never found one, so both macOS builds of every
  pull request and of `main` failed after packaging every app correctly, and
  no macOS release was packaged after the suite folder came in. It now looks
  in the suite folder. The one-line fix reached `main` with the UltraMail
  changes (#667), released without an entry of its own; this one records it.
