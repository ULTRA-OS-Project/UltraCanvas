- **DemoApp's start-up log no longer prints a "Build Date".** It came from
  `__DATE__` / `__TIME__`, which differ on every build, so DemoApp's
  `main.cpp` (and Texter's, which printed the same line) missed ccache on
  every CI run - the one object that did on every pull request.
