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
| `ui/UltraNetMonitorModels.*` | The five `IListModel`s: one row per connection, per process, per recorded flow, per named address, per connection event |
| `ui/UltraNetMonitorPaths.*` | Where the activity store lives by default (the per-user data directory) |
| `ui/UltraNetMonitorWindow.*` | The window: the *Live* tab's split pane, the *History*, *Names* and *Events* tabs, the Record toggle, the snapshot thread |
| `main.cpp` | GUI bootstrap, the name and event sources, and the command line: `--list`, `--by-app`, `--names`, `--events`, `--capabilities`, `--record`, `--history`, `--dns`, `--events-history`, `--totals`, `--store-stats`, `--purge` |
| `UltraNetMonitor.desktop` | The freedesktop shortcut; `make install` places it with the app icon (`media/appicon/UltraNetMonitor.png` / `.svg`, the PNG rendered from the SVG) in the `hicolor` icon theme, which is how the application menu and UltraFiler find the app and its icon |

The socket table is read on a worker thread once a second and applied on
the UI thread by a timer; both lists sit behind an
`UltraCanvasListSortFilterProxy`, so a header click sorts and the filter box
narrows the connection list as you type. Selecting a process narrows it to
that PID; *All applications* widens it again.

## Names

A *Host* column on the connection list and the history shows the domain
name behind a peer where a name source has seen one, and the *Names* tab
lists every address named, with the source. Three sources: reverse DNS
runs by default (`--no-rdns` turns it off) and its names carry a trailing
*?* — a PTR record names the host, not the site, and behind a CDN says
little; the local DNS proxy (`--dns-proxy [<port>]`, `--upstream <ip>`)
learns the name behind every query that passes through it once the system
resolver points at 127.0.0.1 — port 53 needs privilege, any other port
needs the resolver told the port; and on Windows, run elevated, the DNS
client's own events, the one source that knows which process asked.

## Events

The *Events* tab lists connections as they open and close, newest first,
with the time to the millisecond, the application, both endpoints, the
host and — on a closed event — the bytes moved where the source counts
them. Two sources: the snapshot differ (`--no-diff`, `--diff-interval
<ms>`) reports what appeared and went between two reads of the socket
table and misses anything shorter than its interval; the platform's own
source does not — on Linux the kernel's connection tracker, which needs
root and a firewall rule that has activated it, on Windows the kernel's
network events, run elevated. *Show recorded* switches the tab to what
the store holds over the History tab's range.

## Recording

*Record* on the toolbar writes every snapshot into the activity store —
`%LOCALAPPDATA%\UltraNetMonitor\activity.db`, `~/Library/Application
Support/UltraNetMonitor/activity.db` or `$XDG_DATA_HOME/UltraNetMonitor/activity.db`
— and every DNS observation and connection event the sources report
beside them — and the *History* tab shows what was recorded over the last hour, day, week or
month, one row per flow with the host it was seen under. Flows older than
30 days are rolled up into daily totals per application and peer; those
age out after a year. *Purge…* asks twice. The store never leaves the
machine.

## Command line

```
UltraNetMonitor --list             # every connection with its process
UltraNetMonitor --by-app           # per-process roll-up, busiest first
UltraNetMonitor --capabilities     # what this machine's backend can deliver
UltraNetMonitor --list --no-listen --no-loopback
UltraNetMonitor --list --resolve       # wait for reverse DNS before printing
UltraNetMonitor --names --resolve      # the name table
UltraNetMonitor --dns-proxy 5353 --names --resolve   # with the proxy running (point the resolver at it)

UltraNetMonitor --record                       # to the default store, until Ctrl-C
UltraNetMonitor --record ~/net.db --seconds 600 --interval 2000
UltraNetMonitor --history --since 48 --app firefox --limit 50
UltraNetMonitor --history --csv flows.csv      # export instead of print
UltraNetMonitor --dns --since 48               # the recorded DNS observations
UltraNetMonitor --events --seconds 30          # connection events as they happen
UltraNetMonitor --events-history --since 2     # the recorded events
UltraNetMonitor --events-history --csv events.csv
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
dash — never a zero — where a backend collects no counter. A browser that
resolves over HTTPS on its own never asks the system resolver, so neither
the proxy nor the Windows events see its names; only reverse DNS is left
for those peers. Connection events are a later phase of the module.
