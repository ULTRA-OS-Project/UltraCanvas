- **The Windows and Linux packages carry the UltraIM provider modules.**
  `package-win.sh` and `package-linux.sh` copy `Plugins/UltraIM` next to the
  executables (before the dependency closure, so the modules' own libraries
  come along), and `package-linux.sh` lists ChatCube. Either script now
  fails when ChatCube is packaged without `ultraim_direct`, instead of
  shipping an app that can only say its module was not found.
