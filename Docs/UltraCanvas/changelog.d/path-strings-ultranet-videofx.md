- **The UTF-8 path check runs when UltraNet or VideoFX changes.**
  `scripts/check_path_string.py` has always scanned `UltraNet/` and
  `VideoFX/`, but `path-strings.yml` did not list them among the paths that
  start it, so a pull request touching only those modules never ran the
  check, and a `.string()` or `fs::path(std::string)` added there went
  unreported until some later change started it. Both are in the trigger
  list now, which notes that it follows the scanner's `SEARCH_ROOTS`.
