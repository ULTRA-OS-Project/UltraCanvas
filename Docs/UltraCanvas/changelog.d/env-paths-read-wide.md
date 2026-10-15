- **The last environment variables that name a path are read as UTF-8.**
  Four places still read them through the narrow `getenv`, which on Windows
  answers in the ANSI code page: the disk cache's folder (`LOCALAPPDATA`,
  `HOME`, `XDG_CACHE_HOME`, `TMPDIR`), the LaTeX plugin's `MICROTEX_FONTDIR`,
  the DWG converter's `ULTRACANVAS_DXF2DWG` / `ULTRACANVAS_DWG2DXF` (whose
  existence check also opened the path narrowly), and the Windows
  diagnostics' `ULTRACANVAS_CRASH_DUMP_DIR`, `FONTCONFIG_FILE` and
  `ULTRACANVAS_DEBUG_LOG` - where the crash dump folder was then widened as
  if it were UTF-8, so a folder named outside the code page came out mangled.
  All go through `GetEnvUtf8` now. Reads that stay narrow are the ones that
  should: ASCII flags on the crash path, and Linux-, macOS- and Android-only
  code, where the environment is UTF-8 already.
