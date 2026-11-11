- **`NetworkMonitorTests` no longer fails on a busy machine, and the snapshot
  differ no longer loses a closing connection's process there.** Two causes,
  both reproduced by holding 19 500 listeners on the machine's non-loopback
  address across the ephemeral port range, where the test had failed 9 runs
  of 10:
  - **The test matched sockets by port number alone.** Its check that
    `includeLoopback = false` removes the test's own listener and client
    looked for any TCP socket in the snapshot with that local port. The
    snapshot lists every process, and a socket on `127.0.0.1:P` coexists
    with another process's socket on another address with the same port
    `P`, so with enough connections on the machine - a CI runner - the check
    failed although the filter had done its work; it cost #1184 one Linux
    leg. The check now looks for a *loopback* socket on the port and prints
    the row it found when it fails, and a second check states the filter's
    contract without any port: no loopback connection of any process is
    left.
  - **The snapshot differ reported `Closed` without a process.** A socket
    closed between the differ's read of the socket table and the scan that
    attributes sockets to processes reads as ownerless; the differ stored
    that over what its previous snapshot knew, and the `Closed` event went
    out unattributed. It already kept the loopback chain from the earlier
    sighting for the same reason; it now keeps the process too
    (`NetworkMonitorEvents.cpp`). On a quiet machine the gap is
    microseconds; with thousands of sockets a snapshot takes tens of
    milliseconds and it happened in 11 runs of 100. The test prints the
    events it did receive when its `Closed` check fails, which is how this
    was seen.

  With both, 100 runs of 100 pass under that load, 25 of 25 with 2 000
  listeners, and the quiet machine is unchanged.
