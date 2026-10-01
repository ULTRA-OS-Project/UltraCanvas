#### 2026-10-01 *0.1.0*
- **UOS-Settings, the ULTRA OS settings application.** Settings that belong
  to the system rather than to one application get their own program,
  `UOS-Settings` (`Apps/UOSSettings`), laid out like UltraFiler's settings: a
  tree of pages on the left, the page on the right, Close at the foot. Every
  change is saved at once.
  - **Desktop.** UltraDesktop's settings - the taskbar's edge, the
    wallpaper (Browse... opens the file dialog), the RAM disc, the file
    manager, the number of virtual desktops - moved here from the desktop's
    own window. They are written to UltraDesktop's settings file (only those
    fields; the sticky notes in it stay the desktop's), and a running desktop
    takes them over within a second. The desktop's *ULTRA OS settings*
    button opens UOS-Settings on this page.
  - **File dialogs > Last used folder.** A switch decides whether the file
    dialogs of all applications share one last used folder (*One common
    folder*) or keep their own (*Their own folders*). With their own, a table
    with a scrollbar lists the applications - the ones that use the
    framework's file dialog, plus any other that has opened one - each with
    the folder it opens in and its own *Global | Individual* switch.
  - The setting is `FileDialog.conf` in the UltraCanvas settings folder,
    read and written through `UltraCanvasFileDialogSettings.h`, so the file
    dialog and the settings agree on it.
