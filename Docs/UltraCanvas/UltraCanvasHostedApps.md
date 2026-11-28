# UltraCanvasHostedApps — applications inside their host program

<!-- doc-check: bool OpenViewer(const std::vector<std::string>& files); -->

On a desktop every application is a program of its own, started by the
desktop as a process. On Android one app carries everything - MobilePC is
UltraDesktop *and* the applications its Apps window starts, in one process -
so "start an application" has to mean "call its entry point". This registry
is where such applications are listed, and what a desktop's Apps window
starts them through.

```cpp
#include "UltraCanvasHostedApps.h"
using namespace UltraCanvas;

UltraCanvasHostedApps::Register({
    "ultraviewer", "UltraViewer", "Pictures, videos, music and documents",
    "media/appicon/UltraViewer.png", {"Graphics", "Viewer"},
    [](const std::vector<std::string>& files) { return OpenViewer(files); },
    {"UltraViewer"}});   // LaunchProgram("UltraViewer", ...) starts it too

std::string error;
UltraCanvasHostedApps::Launch("ultraviewer", {}, &error);
```

## Contents

- [Registering an application](#registering-an-application)
- [How a desktop sees them](#how-a-desktop-sees-them)
- [Standing in for a program](#standing-in-for-a-program)
- [API](#api)

## Registering an application

A `HostedAppInfo` is an id (stable, lower case), the name a launcher shows, a
comment, an icon file, freedesktop categories (UltraDesktop's Apps window
groups by them) and `launch`: the code an application's own `main()` runs
between `app.Initialize(...)` and `app.Run()` - make the main window, open the
files. It must not run a loop of its own and must not end the process: the
host runs one loop for all its applications, and closing an application's
windows is how it quits. An application whose window class opens its own
windows (UltraPaint's and ArtCreator's `OpenWindow`) needs nothing else; one
that keeps its windows in `main()` moves that into a class first.

`Launching()` names the application whose `launch` is running, so the windows
it makes can be told apart from the host's (the Android desktop mode files
each window under it, and a taskbar groups them).

## How a desktop sees them

Where the platform keeps no applications of its own to list (Android),
`UltraCanvasDesktopShell::ListApplications` lists the registered ones - through
the desktop shell's embedded host, see
[UltraCanvasDesktopShell](UltraCanvasDesktopShell.md#inside-one-program) - each
as a `UCDesktopEntry` whose `shortcutPath` is `hosted:<id>`, and
`LaunchApplication` with that entry calls `Launch`. A desktop written for
desktop platforms, like UltraDesktop, therefore lists and starts them
unchanged.

## Standing in for a program

Applications start each other by program name as well as from the Apps
window: UltraDesktop's taskbar button runs `UltraFiler` (with `--ram-disc`
for the RAM disc button), and UltraFiler runs `UltraViewer` with a picture
that is double-clicked - both through
[`UltraCanvasDesktopShell::LaunchProgram`](UltraCanvasDesktopShell.md). In one
program there is no executable to find, so `programs` names the programs an
application stands in for. `LaunchProgram` asks `IdForProgram` first and, when
an application answers, calls its `launch` with the program's arguments in
place of files - options included, so `launch` reads them as that program's
`main()` would (MobilePC's UltraFiler takes folders and `--ram-disc`). A name
is matched by its file name, in any letter case, `.exe` aside:
`"/opt/bin/UltraFiler"` and `"ultrafiler.exe"` are UltraFiler. Any other name
is looked for as an executable, as before.

## API

| Call | Meaning |
|---|---|
| `Register(info)` | adds it, or replaces the one with that id |
| `List()`, `Has(id)` | |
| `Launch(id, files, &error)` | runs its entry point on the calling (UI) thread; false with the reason |
| `Launching()` | the id whose `Launch` is running, `""` outside one |
| `PathFor(id)` / `IdFromPath(path)` | `"hosted:<id>"` and back |
| `IdForProgram(program)` | the application whose `programs` name `program` (a name or a path, its file name in any letter case, `.exe` aside); `""` when none does |

Header: `UltraCanvasHostedApps.h`; core in `core/UltraCanvasHostedApps.cpp`.
