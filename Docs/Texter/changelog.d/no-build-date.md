- **The start-up log no longer prints a "Build Date".** It came from
  `__DATE__` / `__TIME__`, which differ on every build, so `main.cpp` missed
  the compiler cache on every CI run.
