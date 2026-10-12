- **CI reports how big the file-format plug-ins are.** A new "Format plug-in
  sizes" table in each build row's job summary lists Vector, CDR (with its
  bundled libcdr), XAR, EPS, Models, OCR and the Vectorizer (with its Rust
  library). Each row gives the archive's code and data, which is what one
  copy adds to an application that registers the plug-in, and its file size.
  The plug-ins are static, so each such application carries its own copy.
  The table is the measurement for deciding whether to make them shared
  libraries; OCR and the Vectorizer build only in CI.
  `scripts/report_plugin_sizes.py [build-dir]` prints the same table
  locally. The step only measures and never fails a job.
