- **`package-macos.sh` skips an app that was not built, without hiding real
  failures.** Every `build_app_bundle` call, and `build_cli_tool "ultramsg"`,
  now goes through `package_if_built`, which checks for the executable (in the
  build root or `bin/`) before calling. Before, a missing Texter, UltraFiler or
  any other app stopped the whole run under `set -e`, although the function
  said it was "skipping". The UltraAuthenticator and UltraPassword calls used
  `build_app_bundle … || echo …` instead, and that was worse: bash turns
  `set -e` off for a function's entire body when it is called on the left of
  `||`, so a failed signing, `iconutil` or dylib-copy step would have been
  carried past and an unsigned or half-built bundle shipped. Those calls now
  go through the same helper. The run ends by listing what was not built, and
  fails when it produced no bundle at all.
