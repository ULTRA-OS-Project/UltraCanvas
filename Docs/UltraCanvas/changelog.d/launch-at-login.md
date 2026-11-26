- **UltraCanvasLaunchAtLogin: starting a program when the user signs in.**
  `SetLaunchAtLogin(entry, enabled, error)` puts a program in the computer's
  own startup list or takes it out; `IsLaunchAtLoginEnabled(id)` reads it
  back, so an entry switched off by the user elsewhere reads as off. Linux
  and the BSDs write an XDG autostart entry (`~/.config/autostart`, with
  `Exec=` quoted to the specification and `Hidden=true` over a system entry
  of the same name), Windows the `HKCU\...\Run` value (respecting the Task
  Manager's on / off), macOS a LaunchAgent with `RunAtLoad`. An AppImage is
  started as the AppImage, a program in an `.app` bundle through
  `open -a`. Not available in a Flatpak, a Snap or an MSIX package, nor on
  Android, iOS or WebAssembly (`LaunchAtLoginAvailable()` is false).
  `Docs/UltraCanvas/UltraCanvasLaunchAtLogin.md`.
