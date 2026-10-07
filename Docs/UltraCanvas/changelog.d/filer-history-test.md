- **`FilerHistoryTest` runs.** The test of UltraFiler's recently-used lists
  (`Tests/FilerHistoryTest.cpp` - the Files / Folders / Apps lists survive a
  restart and keep the History & Favorites limit while recording, on reading
  the file back and when the limit is lowered) was written in September but
  never added to any CMake file, so no build compiled it and CI never ran it.
  It is in `Tests/FilerTests.cmake` now, so Linux runs it in the full test
  pass and the Windows job runs it with the other Filer tests, where
  UltraFiler keeps its history under `%APPDATA%`. For Windows its paths now go
  through `PathToUtf8` / `PathFromUtf8` and its config folder is redirected
  with the wide environment API, and `scripts/check_path_string.py` scans it,
  as it does every test that runs on Windows.
