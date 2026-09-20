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
| `ui/UltraNetMonitorModels.*` | The three `IListModel`s: one row per connection, per process, per recorded flow |
| `ui/UltraNetMonitorPaths.*` | Where the activity store lives by default (the per-user data directory) |
| `ui/UltraNetMonitorWindow.*` | The window: the *Live* tab's split pane, the *History* tab, the Record toggle, the snapshot thread |
| `main.cpp` | GUI bootstrap, and the command line: `--list`, `--by-app`, `--capabilities`, `--record`, `--history`, `--totals`, `--store-stats`, `--purge` |

The socket table is read on a worker thread once a second and applied on
the UI thread by a timer; both lists sit behind an
`UltraCanvasListSortFilterProxy`, so a header click sorts and the filter box
narrows the connection list as you type. Selecting a process narrows it to
that PID; *All applications* widens it again.

## Recording

*Record* on the toolbar writes every snapshot into the activity store —
`%LOCALAPPDATA%\UltraNetMonitor\activity.db`, `~/Library/Application
Support/UltraNetMonitor/activity.db` or `$XDG_DATA_HOME/UltraNetMonitor/activity.db`
— and the *History* tab shows what was recorded over the last hour, day,
week or month, one row per flow. Flows older than 30 days are rolled up
into daily totals per application and peer; those age out after a year.
*Purge…* asks twice. The store never leaves the machine.

## Command line

```
UltraNetMonitor --list             # every connection with its process
UltraNetMonitor --by-app           # per-process roll-up, busiest first
UltraNetMonitor --capabilities     # what this machine's backend can deliver
UltraNetMonitor --list --no-listen --no-loopback

UltraNetMonitor --record                       # to the default store, until Ctrl-C
UltraNetMonitor --record ~/net.db --seconds 600 --interval 2000
UltraNetMonitor --history --since 48 --app firefox --limit 50
UltraNetMonitor --history --csv flows.csv      # export instead of print
UltraNetMonitor --totals                       # the rolled-up daily totals
UltraNetMonitor --store-stats
UltraNetMonitor --purge --yes                  # irreversible
```

## What it cannot see

Not elevated, only this user's processes can be attributed: on Linux and
Windows other users' sockets still appear, as *(unattributed)*; on macOS
they do not appear at all, because sockets there are enumerated per process.
The status line says how many processes could not be inspected. The *Sent*
and *Received* columns are filled on Linux (netlink `sock_diag`) and show a
dash — never a zero — where a backend collects no counter. Connection events
and domain names are later phases of the module.
