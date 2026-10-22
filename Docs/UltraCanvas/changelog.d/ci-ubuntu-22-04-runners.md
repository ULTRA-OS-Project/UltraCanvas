- **CI: the gate, the lint checks and the Android backend check run on
  `ubuntu-22.04`.** They ran on `ubuntu-latest`, a separate pool from the
  `ubuntu-22.04` one the build legs use, and on 2026-10-21 that pool held
  them for up to three hours - the gate alone kept one pull request's six
  builds from starting for ninety minutes - while the build legs ran at once.
  Every script they run passes under that image's Python 3.10.
