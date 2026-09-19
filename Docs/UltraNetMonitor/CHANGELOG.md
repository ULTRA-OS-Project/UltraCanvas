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
