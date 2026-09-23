#### 2026-09-23 *0.11*
- **The Events tab shows the loopback chain.** A *Via* column on the events
  (NetworkMonitor 0.9, with the framework change that gives connection
  events their loopback chain), read as on the other tabs: a mail client's
  connection to the antivirus proxy opens as "→ AvastSvc (4720)", the
  proxy's accepted side as "← thunderbird (4120)", and the proxy's own
  connection to the mail server as "for thunderbird (4120)" - on the closed
  event too, after the sockets are gone. *Show recorded* reads the chain
  back from the store, the filter box searches it, `--events` and
  `--events-history` print it, and `--events-history --csv` writes
  `loopback_role`, `local_peer` and `for`.

#### 2026-09-23 *0.10*
- **The History tab says whom a flow was for.** A *Via* column on the
  history shows the loopback chain each recorded flow was last seen with
  (NetworkMonitor 0.8, with the framework change that keeps loopback chains
  in the activity store): "→ AvastSvc (4720)" on the mail client's flow to
  the proxy, "← thunderbird (4120)" on the proxy's side, and "for
  thunderbird (4120)" on the proxy's flows to the mail server - kept after
  the client's own socket is gone, so last Tuesday's history still names the
  application behind the proxy. The history's search finds those flows by
  the client's name, `--history` prints the column, `--history --csv` writes
  `loopback_role`, `local_peer` and `for`, and `--totals` shows the last
  *for* a day's flows carried. The store migrates in place on the first run.

#### 2026-09-23 *0.9*
- **Who is behind the proxy.** A *Via* column on the connection list and
  the process list decodes loopback chains (NetworkMonitor 0.7, framework
  0.9.40): a mail client's connection to 127.0.0.1:12993 reads
  "→ AvastSvc (4720)", the proxy's accepted socket "← thunderbird (4120)",
  and the proxy's own connections to the mail server "for thunderbird
  (4120)" - the applications that traffic is really for, an inference the
  tooltip labels as such. The process list's *Via* says what an
  application talks through, or whom a service serves. Both CSV exports
  gain the same columns, and `--list` / `--by-app` show them.
- **Names for the processes Windows will not open.** A service the
  monitor may not open, run unelevated, is named from the process list
  instead of "pid 4720"; the tooltip says its path and user still need
  elevation.

#### 2026-09-23 *0.8*
- **Export.** A right click on the process list opens a menu with
  *Export → App list…* and *Export → App list details…* (NetworkMonitor 0.6,
  framework 0.9.39). Both write a CSV to the file chosen in the native
  save dialog: the app list is one row per application as the list shows
  it, in its current sort order, with the connection, established,
  listening and peer counts, the distinct peer addresses and hosts, and the
  byte totals where every connection had them; the details are one row per
  connection, grouped by application in that order, with both endpoints,
  the host and its source, the state and the counters. A dialog says how
  many rows went where, or why the file could not be written.
  - **Headless too.** `--list --csv <file>` and `--by-app --csv <file>`
    write the same two files instead of printing.

#### 2026-09-23 *0.7*
- **Events.** Connections as they open and close, not only as the next
  snapshot finds them (NetworkMonitor 0.5, framework 0.9.38): a new
  *Events* tab lists every event the sources report, newest first, with
  the time to the millisecond, whether the connection was opened here or
  accepted from a peer, the application, both endpoints, the host, and on
  a closed event the bytes it moved where the source counts them. *Show
  recorded* switches the tab to what the store holds over the History
  tab's range; *Clear* empties the live list.
  - **Two sources.** The snapshot differ runs everywhere (`--no-diff`
    turns it off, `--diff-interval <ms>` sets its pace, 250 ms by default)
    and reports what appeared and what went between two reads of the
    socket table - so it misses connections shorter than its interval,
    and says so. The platform's own source does not: on Linux the
    kernel's connection tracker (`nf_conntrack`), which needs root and a
    firewall rule that has activated it; on Windows, run elevated, the
    kernel's network events, which also bring the byte counters the
    Windows socket table lacks. The subtitle names what is running.
  - **Recorded.** While recording, every event goes into the store beside
    the flows and the DNS observations; `--events-history` prints them
    with the `--history` filters (`--csv` exports), `--store-stats` counts
    them, and an older store gains the table on open.
  - **Headless.** `--events` prints events as they happen until Ctrl-C or
    `--seconds`; `--capabilities` lists the event sources.
  - The store's directory is now created for its owner alone (`0700`) on
    Linux and macOS; `%LOCALAPPDATA%` is per-user already.
  - Ctrl-C in the window no longer exits from inside the signal handler,
    which ran the static destructors while the worker and the sources'
    threads were alive: the handler sets the framework's signal flag
    (`RequestExitFromSignal`, framework 0.9.38) and the window shuts down
    in order.

#### 2026-09-22 *0.6*
- **Names.** Connections carry the domain name behind the peer where a name
  source has seen one (NetworkMonitor 0.4, framework 0.9.21): a *Host*
  column on the Live connection list and the History flows, the peers'
  names in the process list's tooltip, and a new *Names* tab listing every
  address the sources have named, with the source, when it was observed
  and how long it is kept. A name that came from reverse DNS shows a
  trailing *?* and says so on hover: it is a guess at the host, not what
  the application asked for; a name from a DNS query the monitor saw is
  shown plain.
  - **Three sources.** Reverse DNS runs by default (`--no-rdns` turns it
    off) and never looks up loopback, link-local or private addresses.
    `--dns-proxy [<port>]` runs the local DNS proxy on 127.0.0.1 - port 53
    needs privilege; any other port needs the system resolver pointed at
    it - and learns the name behind every query that passes through, over
    UDP and TCP, forwarding to the system's resolver or `--upstream <ip>`.
    On Windows, run elevated, the DNS client's own events are the third
    source, and the only one that knows which process asked; the *Asked
    by* column shows it.
  - **Recorded.** While recording, every DNS observation goes into the
    store beside the flows, and a flow keeps the best name it was seen
    with. `--dns` prints the recorded observations with the `--history`
    filters, `--history` and `--totals` show the host, the CSV gains
    `remote_name` and `name_source` columns, and `--store-stats` counts the
    observations. An activity store written by 0.3 opens and gains the
    columns.
  - **Headless.** `--names` prints the name table; `--list`, `--by-app` and
    `--names` take `--resolve` to wait up to three seconds for reverse DNS
    first; `--capabilities` lists the name sources and the system resolver.

#### 2026-09-22 *0.5*
- **UltraNetMonitor has an app icon.** The uploaded artwork — throughput bars
  over a globe — is `media/appicon/UltraNetMonitor.svg`, and
  `media/appicon/UltraNetMonitor.png` is its 256 px render. It arrived as
  `NetMonitor.svg`; it is named for the application now, because the name is
  not decorative: `Icon=UltraNetMonitor` in the desktop entry is resolved
  through the installed icon themes, so the file has to be called what the
  entry asks for. The Xara export was a page, not an icon — a non-square
  `viewBox` in pt, a Times New Roman declaration no glyph in the file uses, an
  empty `<defs>` and an SVG 1.1 DTD reference a parser may try to fetch off the
  network. The drawing is kept verbatim; only the root element was rewritten,
  onto a square canvas with the small margin the other app icons have.
- **It is drawn everywhere the app is shown.** `main.cpp` hands the PNG to
  `SetDefaultWindowIcon` for the window and the taskbar entry that follows it;
  `UCAPP_ICON_PATH` names the same file as the core's fallback, so a window
  never comes up unbranded; `ultracanvas_embed_app_icon` builds the `.exe`
  icon Explorer and the Windows taskbar read off the binary itself. A
  freedesktop entry (`Apps/UltraNetMonitor/UltraNetMonitor.desktop`) puts the
  monitor in the application menu, and the install rules place the PNG in
  `share/icons/hicolor/256x256/apps` and the SVG in
  `share/icons/hicolor/scalable/apps` so that name resolves — the route
  UltraFiler takes to show an application's own icon. The app now takes the
  build's asset copy as a dependency, so the PNG is in its resources dir when
  it runs out of the build tree. `install(TARGETS)` came with it: `TryExec` is
  resolved against `PATH`, so the entry is only true if the binary is
  installed alongside it.

#### 2026-09-20 *0.4*
- **One version number, one place.** This changelog's first line is now the
  only place UltraNetMonitor's version lives: the build reads it
  (`cmake/UltraCanvasVersion.cmake`) and passes it to the sources. The
  `"0.0-dev"` fallbacks in `main.cpp` and `ui/UltraNetMonitorWindow.cpp` are
  gone; a build without the definition now fails at compile time instead of
  reporting 0.0-dev. No functional change otherwise.

#### 2026-09-19 *0.3*
- **It remembers.** A *Record* toggle on the toolbar writes every snapshot
  into the activity store (NetworkMonitor 0.3, framework 0.9.13) at the
  platform's per-user data path — `%LOCALAPPDATA%\UltraNetMonitor`,
  `~/Library/Application Support/UltraNetMonitor`, `$XDG_DATA_HOME/UltraNetMonitor`
  — and a new *History* tab shows what was recorded over the last hour, day,
  week or month: one row per flow with first and last sighting, how many
  snapshots saw it, its last state and its latest counters, sortable and
  filterable like the live lists. *Purge…* asks twice before it deletes.
  Retention is applied when recording starts and stops and when the window
  closes: flows older than 30 days become daily totals, and those age out
  after a year.
  - **Headless too.** `--record [<db>] [--seconds n] [--interval ms]` records
    until Ctrl-C; `--history`, `--totals` and `--store-stats` read the store
    back, `--history --csv <file>` exports it, and `--purge` needs `--yes`.
    `--capabilities` now says whether this build has a store at all.

#### 2026-09-19 *0.2*
- **Windows and macOS, and bytes on Linux.** The window and the command
  line now open on all three desktop platforms (NetworkMonitor 0.2, framework
  0.8.99): IP Helper on Windows, libproc on macOS. Two new columns, *Sent*
  and *Received*, on both lists and in `--list` / `--by-app`, filled on Linux
  from netlink `sock_diag` and shown as a dash — never a zero — where a
  backend collects no counter; the subtitle says which. Sorting the byte
  columns sorts by the number, with the uncounted rows last.
  - The usage text and the notes after `--list` now speak of "elevated"
    rather than "root", since the Windows backend reports the same limit in
    its own terms.

#### 2026-09-19 *0.1*
- **First build: the connection table, live, with the process behind each
  socket.** UltraNetMonitor is the application on top of the NetworkMonitor
  module (framework 0.8.98): a window with the machine's processes on the
  left — connection, established, listening and peer counts, busiest first —
  and every socket on the right with its application, PID, protocol, local
  and remote endpoints, state and owning user. Both lists sort on a header
  click; the connection list filters as you type, and selecting an
  application narrows it to that one. A background thread takes a snapshot
  every second and the window applies it on the UI thread; *Pause* freezes
  the picture for reading.
  - **What it cannot see, it says.** The status line names the backend and
    whether the monitor can attribute every process or only its own user's:
    not running as root, sockets of other users' processes still appear, as
    *(unattributed)*, with the owning UID.
  - **Headless too.** `--list` prints the connection table and `--by-app` the
    per-process roll-up, `--capabilities` what this machine can deliver, so
    it is usable over ssh and checkable in CI. Linux only in this build; the
    window opens on every platform and reports *No NetworkMonitor backend*
    where there is none yet.
