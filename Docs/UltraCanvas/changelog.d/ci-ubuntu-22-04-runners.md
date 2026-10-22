- **CI: the gate, the lint checks and the Android backend check run on
  `ubuntu-22.04`.** They ran on `ubuntu-latest`, a separate pool from the
  `ubuntu-22.04` one the build legs use, and on 2026-10-21 that pool held
  them for up to three hours - the gate alone kept one pull request's six
  builds from starting for ninety minutes - while the build legs ran at once.
  Every script they run passes under that image's Python 3.10.
  The Doc examples check moves too: a baseline entry drops the
  `UltraCanvas::` one clang writes before a type and another leaves out
  (`check_doc_examples.py` 1.8.0), so its clang 14 and the 24.04 image's
  clang 18 read the baseline alike.
