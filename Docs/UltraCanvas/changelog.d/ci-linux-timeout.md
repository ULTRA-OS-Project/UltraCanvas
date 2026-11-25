- **CI: the Linux build job gets 180 minutes.** It builds, runs every test
  and the layout audits, then packages the AppImage, the SDK and the store
  packages, and had reached the 120-minute cap: on 2026-11-24 one pull
  request's run took 118 minutes and another's was cut off in its last
  packaging step with its build and tests green. Windows on ARM already got
  180; macOS keeps 300, Windows x86_64 120.
