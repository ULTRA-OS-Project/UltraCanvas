- **The WebAssembly configure no longer fails on CI's in-checkout sysroot.**
  The core's Emscripten link directories (the wasm sysroot's `lib/`, for
  cairo, pango, glib, libvips and the rest) are build-interface only now:
  `wasm-build.yml` keeps the sysroot inside the workspace, and CMake refuses
  to generate an exported target whose `INTERFACE_LINK_DIRECTORIES` holds a
  path under the source directory. Run 14 of the workflow, the first to
  configure the repository root rather than `OS/WASM/demo`, failed on every
  variant with that error; the installed package has no use for a build
  machine's sysroot anyway.
