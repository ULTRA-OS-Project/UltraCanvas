- **The vendored libcurl had no `sftp://`, so UltraStoreAdmin's SFTP upload
  was refused with `Protocol "sftp" is disabled` even with libssh2
  installed and linked.** The vendored `third_party/curl` carried the
  output of an in-tree autotools `./configure` - `lib/curl_config.h`, the
  Makefiles, `.deps/`, `config.status`, `libtool` and the rest - and
  `curl_setup.h` includes `"curl_config.h"` with quotes, so the compiler
  took that stale file beside it instead of the one CMake generates, and
  every option CMake had decided (libssh2 among them) was lost while the
  library still linked what CMake found. The generated files are removed;
  `VendoredCurl.cmake` refuses to configure while such a `curl_config.h`
  exists in the source tree, asks curl's CMake for libssh2 explicitly and
  prints which SSH backend it got, warning when there is none; the Linux CI
  leg installs `libssh2-1-dev` and fails when the vendored libcurl has no
  SSH backend.
