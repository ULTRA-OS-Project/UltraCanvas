- **A hosted application answers its program's name.** `HostedAppInfo` has
  `programs`, the executable names an application inside its host stands in
  for, and `UltraCanvasDesktopShell::LaunchProgram` starts that application -
  with the program's arguments - before looking for an executable
  (`UltraCanvasHostedApps::IdForProgram`). In MobilePC, UltraDesktop's
  taskbar button for "UltraFiler" and UltraFiler's "UltraViewer" for a
  double-clicked picture now start the applications in the same process;
  before, they failed with "not installed". `HostedAppsTest` covers the
  registry and the routing.
- **Android: access to every file on the phone's storage.**
  `AndroidPermissions::HasAllFilesAccess`, `RequestAllFilesAccess` and
  `SharedStoragePath` (`OS/Android/UltraCanvasAndroidPermissions.h`), with
  their Java half in `UltraCanvasActivity`: *All files access* from Android 11
  - Settings' switch, opened on the phone - and the storage permission before
  it. What a file manager such as MobilePC's UltraFiler needs to browse
  `/storage/emulated/0` by path.
