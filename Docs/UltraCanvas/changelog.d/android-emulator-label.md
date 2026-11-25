- **`ci:emulator`: the Android emulator on a pull request, when asked for.**
  A pull request builds Android for arm64 alone and runs no emulator; one
  labelled `ci:emulator` now gets what `main` runs - x86_64 as well, the demo
  on the emulator at two densities and MobilePC on a second display - so an
  Android change is seen running before it merges. Adding the label starts
  that run without a push. Adding any other label starts no Android build
  and cancels none in progress.
