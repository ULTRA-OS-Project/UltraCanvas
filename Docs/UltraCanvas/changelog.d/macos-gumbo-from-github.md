- **macOS builds no longer download gumbo from codeberg.org.** MuPDF was its
  only user, and vcpkg's gumbo port fetches its source from Codeberg, which
  on 2026-11-09 sent nothing for half an hour, run after run: every macOS
  build that had to fetch the sources timed out on it, on `main` and on every
  pull request. The mupdf overlay port (`MacOS/deps/ports/mupdf`, port-version
  2) now builds MuPDF's own gumbo submodule, fetched from GitHub at the
  commit MuPDF 1.28.5 records, like its other four submodules
  (`USE_SYSTEM_GUMBO=no`), and no longer depends on vcpkg's gumbo.
