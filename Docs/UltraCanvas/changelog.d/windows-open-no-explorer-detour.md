- **Windows: a default open that fails is reported, not handed to
  `explorer.exe`.** 0.9.83 answered a registered handler that would not
  start from our process by starting it from Explorer's, which hid the
  reason. The reason was the `mpr.dll` coder shadowing the system's (fixed
  in the package since, and repaired on start now), so the detour is gone:
  `OpenWithDefaultApplication` launches through the shell as a double-click
  does, with the loader's hard-error box still off on the launching thread,
  and a launch that fails names the shell's error.
