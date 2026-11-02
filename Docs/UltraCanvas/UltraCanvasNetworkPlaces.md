# UltraCanvasNetworkPlaces

<!-- doc-check: void AddRow(const std::string& path, const std::string& name); -->

The places Windows Explorer lists outside *This PC*: the **computers on the
network** with the folders they share, and the **Linux distributions** of the
Windows Subsystem for Linux.

```cpp
#include "UltraCanvasNetworkPlaces.h"

for (const WslDistribution& d : ListWslDistributions())
    AddRow(d.path, d.name);                               // "\\wsl$\Ubuntu"

std::vector<NetworkShare> shares;
std::string error;
if (ListNetworkShares("\\\\NAS", shares, error))
    for (const NetworkShare& s : shares) AddRow(s.path, s.name);   // "\\NAS\photos"
```

What lies below a share or a distribution is an ordinary path — a UNC path
`std::filesystem` reads like any folder — so this answers only what is *above*
it: which computers there are, which folders a computer shares, which
distributions are installed. A computer itself (`\\NAS`) is not a folder
anything can list; `IsNetworkServerPath` tells it apart.

## API

| Call | Meaning |
|---|---|
| `NetworkPlacesAvailable()` | Whether this build can list any of these (Windows). |
| `ListNetworkComputers()` | `NetworkComputer { name, path, comment }`, by name: the computers the Windows network lists for its workgroup or domain, and the servers of the user's own connections (a mapped drive, a share opened and remembered), each once. **Blocks** for as long as the network takes. |
| `ListNetworkShares(server, out, error)` | `NetworkShare { name, path, comment }` of `server` (`"\\NAS"`, `"NAS"` or `"//NAS"`), folders only, the administrative and hidden shares (`C$`, `ADMIN$`, `IPC$`) left out. False with a reason when the computer cannot be reached or refuses. **Blocks.** |
| `ListWslDistributions()` | `WslDistribution { name, path, isDefault }`, the default first: the installed distributions from the registry. `path` is `\\wsl$\<name>`, browsed like a share. Starts none of them. |
| `IsNetworkServerPath(path)` / `NetworkServerName(path)` | Whether `path` names a computer and nothing below it (`\\NAS`, `\\NAS\`, `//NAS`), and its name. Not `\\NAS\photos`, not Windows' own `\\?\` and `\\.\` prefixes, not the WSL roots `\\wsl$` and `\\wsl.localhost`. |
| `NetworkServerPath(server)` | `\\NAS` for `NAS`, `//NAS` or `\\NAS\` — the form the lists use. |

Run the two blocking calls off the UI thread: a computer that does not answer is
waited out.

## What the network list misses

`ListNetworkComputers` reports what the Windows network provider knows. A
network whose computers advertise themselves only through WS-Discovery (the
default for current Windows and many NAS boxes once SMB 1 is off) shows fewer
computers here than Explorer, which also asks Function Discovery. Such a
computer is still reached **by name** — `ListNetworkShares("\\\\NAS", …)` does
not need it to be listed — which is why UltraFiler keeps the computers opened
by name (`UltraFilerSettings::networkServers`).

## Backends

| Platform | Backend |
|---|---|
| Windows | `WNetOpenEnumW` / `WNetEnumResourceW` (the computers, and a server's folders when `NetShareEnum` is refused), `NetShareEnum` (the shares), the `HKCU\Software\Microsoft\Windows\CurrentVersion\Lxss` key (the distributions). `mpr.dll` and `netapi32.dll` are **loaded from System32 when first needed**, never imported: an installed package can carry an ImageMagick coder named `mpr.dll`, and Windows matches modules by base name — see `UltraCanvasCoderModuleRepair.h`. |
| everything else | none — every list is empty. A Linux desktop mounts the shares it opens through GVFS ([`ListGvfsVolumes`](UltraCanvasVolumeMonitor.md#gvfs-places)); macOS mounts them under `/Volumes`. |

Wired up by the `ULTRACANVAS_HAS_NETWORK_PLACES` condition in the top-level
`CMakeLists.txt`; without it the core file compiles the empty lists.

## UltraFiler

UltraFiler shows two sections after *Computer*, where Explorer has them:
**Linux** (one row per distribution; not probed, because touching a
distribution starts it — its folders are listed when the row is opened) and
**Network** (one row per computer; opening one lists its shares off the UI
thread, under its row and in the folder display). Typing `\\NAS` into the path
bar opens that computer the same way.
