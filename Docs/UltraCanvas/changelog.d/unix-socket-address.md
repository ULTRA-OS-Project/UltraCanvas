- **`UltraCanvasUnixSocketAddress.h`: the address of a Unix socket at a path
  of any length.** A socket's address holds a path of 107 bytes on Linux (103
  on macOS and the BSDs), and two modules each refused a longer one in their
  own words. `UnixSocketAddress::Resolve(path, error)` fills the address, and
  on Linux reaches a longer path through its folder
  (`/proc/self/fd/<n>/<file>`), so the socket stays where its path says;
  `Fits(path)` says whether a path needs that. Header-only, with nothing of
  the framework in it. UltraMessage's transport reaches a bus under a long
  runtime folder with it (its own entry), and UltraWin's QMP client the
  control socket of a machine in a long folder (UltraWin's changelog). Doc:
  `Docs/UltraCanvas/UltraCanvasUnixSocketAddress.md`.
