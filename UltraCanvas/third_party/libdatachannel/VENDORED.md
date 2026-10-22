# libdatachannel (vendored)

- Upstream: https://github.com/paullouisageneau/libdatachannel
- Version: v0.24.6, with its submodules at the commits that tag pins:
  - `deps/libjuice` b89c792 (MPL-2.0)
  - `deps/libsrtp`  24b3bf8 (BSD-3-Clause)
  - `deps/usrsctp`  fec583d (BSD-3-Clause)
  - `deps/plog`     94899e0 (MIT)
- Licence: MPL-2.0 (`LICENSE`).

The sources are unmodified but for one line: `src/configuration.cpp` includes
`<algorithm>` for its `std::transform`, which newer libc++ (MSYS2 clang64) no
longer pulls in through `<regex>`. Re-apply it on an update unless upstream has
it. Removed to keep the tree small, none of it built
by UltraCanvas: `.git` metadata, `examples/`, `pages/`, `test/`, `.github/`,
`deps/json` (only the examples use it), `deps/libsrtp/{test,crypto/test,fuzzer}`,
`deps/usrsctp/{fuzzer,programs}`, `deps/libjuice/test`, `deps/plog/{samples,doc}`.

Used only by the UltraRTC module (`UltraCanvas/core/UltraRTC/`), which wraps it
behind the `UltraRtc_*` API; no libdatachannel type appears in a public header.
To update: clone the new tag with `--recurse-submodules`, remove the same
directories, replace this folder, and update the versions above,
`THIRD_PARTY_LICENSES.md`, `Docs/Dependencies.md` and
`master_dependencies.yaml`.
