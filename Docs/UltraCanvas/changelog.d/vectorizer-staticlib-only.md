- **The Vectorizer's Rust crate builds the archive alone, which unbreaks the
  Windows x86_64 build.** `vtracer-c` declared `crate-type = ["staticlib",
  "cdylib"]`, although nothing in the tree links or loads a `vtracer_c` shared
  library: the plugin links `libvtracer_c.a`, and an installed package names
  that archive by path. The cdylib brought an import library with it -
  Corrosion lists `deps/libvtracer_c.dll.a` as a byproduct and copies it beside
  the archive - and rustc writes that file only when it links the cdylib, so
  a build where it is absent died in the copy with "No such file or directory"
  after cargo had already reported success. That took out every x86_64 Windows
  build on 2026-11-14, on `main` and on every open pull request, with no error
  from the compiler anywhere. The same cdylib was behind an older trap, where a
  stray `BUILD_SHARED_LIBS=ON` in the cache had Corrosion hand the plugin the
  DLL instead of the archive. Both are gone with it, and the Windows and Linux
  builds lose a link step nobody was waiting for.
