- **`scripts/check_doc_examples.py` rebuilds its precompiled header when any
  header it was built from changes**, not only one under `UltraCanvas/include/`.
  A public header includes further ones (`UltraCanvasImage.h` takes
  `libspecific/Cairo/ImageCairo.h`), and after an edit to one of those clang
  refused the cached header, which the checker reported as "a header changed
  while the docs were being checked" on every run until something under
  `include/` was touched.
