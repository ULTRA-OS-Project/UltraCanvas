- **The doc-examples check caches its precompiled header in CI.**
  `scripts/check_doc_examples.py` reuses the precompiled header of all public
  headers by what it was built from (the headers' content, the flags and the
  clang, printed by `--pch-key`) rather than by mtimes, and confirms it with
  one compile; `doc-examples.yml` caches it under that key, saving about a
  minute per run.
