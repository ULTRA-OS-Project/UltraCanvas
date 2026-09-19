# UltraNetMonitor

Shows which processes hold which network connections, live, from the
operating system's socket table — the view `ss -p` gives in a terminal, as a
window with the processes on one side and every socket on the other. It
observes; it never blocks or modifies traffic.

The module underneath, and what it can and cannot see on each platform:
[`Docs/Modules/NetworkMonitor/README.md`](../../Docs/Modules/NetworkMonitor/README.md).
Changelog and version: [`Docs/UltraNetMonitor/CHANGELOG.md`](../../Docs/UltraNetMonitor/CHANGELOG.md).

## Layout

| Path | What is in it |
|---|---|
| `ui/UltraNetMonitorModels.*` | The two `IListModel`s: one row per connection, one per process |
| `ui/UltraNetMonitorWindow.*` | The window: filter, pause, the split pane of two `UltraCanvasListView`s, the snapshot thread |
| `main.cpp` | GUI bootstrap, and the `--list` / `--by-app` / `--capabilities` command line |

The socket table is read on a worker thread once a second and applied on
the UI thread by a timer; both lists sit behind an
`UltraCanvasListSortFilterProxy`, so a header click sorts and the filter box
narrows the connection list as you type. Selecting a process narrows it to
that PID; *All applications* widens it again.

## Command line

```
UltraNetMonitor --list             # every connection with its process
UltraNetMonitor --by-app           # per-process roll-up, busiest first
UltraNetMonitor --capabilities     # what this machine's backend can deliver
UltraNetMonitor --list --no-listen --no-loopback
```

## What it cannot see

Not running as root, only this user's processes can be attributed: other
users' sockets still appear, as *(unattributed)* with the owning UID, and
the status line says how many processes could not be inspected. Byte
counters, connection events and domain names are later phases of the
module. Linux only in this build; the window opens on every platform and
reports *No NetworkMonitor backend* where there is none yet.
