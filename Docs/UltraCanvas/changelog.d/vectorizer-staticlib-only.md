- **The Vectorizer plug-in's Rust crate builds only the static library it
  links.** `vtracer-c/Cargo.toml` also declared a `cdylib`, a DLL nothing in
  the tree loads or ships, and Corrosion listed that DLL's MinGW import
  library (`libvtracer_c.dll.a`) among the build's byproducts. The MSYS2
  Rust 1.100.0 package (2026-11-14) no longer writes that import library for
  the `x86_64-pc-windows-gnullvm` target, so every Windows x64 build since
  stopped at "Error copying file ... libvtracer_c.dll.a" after the crate had
  compiled (#1244, #1245, #1248). The crate type is gone; the plug-in links
  `libvtracer_c.a` as before on every platform.
