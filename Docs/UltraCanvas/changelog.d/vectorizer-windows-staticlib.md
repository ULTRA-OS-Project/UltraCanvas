- **The Windows x86_64 build failed at the Vectorizer plugin's Rust library.**
  `vtracer-c/Cargo.toml` lists `cdylib` beside `staticlib`, so Corrosion
  imported both and copied the DLL's import library from
  `release/deps/libvtracer_c.dll.a`, where Rust on
  `x86_64-pc-windows-gnullvm` no longer writes it: *"Error copying file ...
  No such file or directory"*, on every pull request. The plug-in only ever
  links the archive, so `corrosion_import_crate` now takes
  `CRATE_TYPES staticlib`: cargo is run with `--crate-type=staticlib`, the DLL
  is not built at all, and `BUILD_SHARED_LIBS` can no longer make Corrosion
  link it instead of the archive.
