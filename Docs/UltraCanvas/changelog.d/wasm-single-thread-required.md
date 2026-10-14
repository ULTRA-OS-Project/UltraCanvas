- **The WebAssembly workflow's `single-thread` variant is required.** It was
  one of the three experimental variants that never failed the run, from
  the days when its sysroot had never been built; now that it builds and
  passes its browser smoke test, a regression in the single-threaded
  program - the one a static host without cross-origin isolation headers
  serves - fails the run like one in the baseline. `no-vips` and
  `four-fonts` stay measurements.
