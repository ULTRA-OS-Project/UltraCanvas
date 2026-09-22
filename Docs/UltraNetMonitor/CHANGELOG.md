#### 2026-09-22 *0.4*
- **Names.** Connections carry the domain name behind the peer where a name
  source has seen one (NetworkMonitor 0.4, framework 0.9.15): a *Host*
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
