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
