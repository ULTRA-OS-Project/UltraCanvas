- **CI: the macOS Intel row builds on main only.** A pull request's matrix
  is five rows and finishes with the Windows ARM one, in about 35 minutes;
  the macOS Intel row runs on every push to main and on dispatches, before
  anything is released. Its runner compiles five to ten times slower than
  every other row and waited 16-27 minutes for a free machine on each run
  of 2026-10-13, so it finished a pull request's matrix 45-100 minutes after
  the other five rows had, with nothing of its own to say: the two macOS
  rows share one code base and one packaging script, and what differs
  between them - the libraries' vcpkg triplet, the x86_64 SDK - is still
  checked on main. A pull request that changes what only the Intel
  toolchain would catch asks for the row with the `ci:intel` label, which
  starts a run with the full matrix. The matrix is now made by the gate job
  (a row cannot be dropped by event from a matrix written under the build
  job: `exclude` runs before `include`, which put the row back without a
  build type).
