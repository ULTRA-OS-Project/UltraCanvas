- **The UTF-8 path check sees the conversions hidden in a call.**
  `scripts/check_path_string.py` caught `.string()` and `fs::path(str)`, but
  not the same code-page conversion made implicitly: `fs::exists(str)`,
  `fs::create_directories(dir)`, `fs::directory_iterator(root)` and the other
  `std::filesystem` functions given a `std::string`, a declaration
  `fs::path p(str);`, or a file stream opened from one -
  `std::ifstream in(path)`. On Windows each reads the UTF-8 name in the ANSI
  code page, so a folder such as "Entwürfe" is not found; EmailCleaner skipped
  such folders' mail this way. Three new kinds, `implicit-path-from-string`,
  `stream-from-string` and the declaration form of `path-from-string`, decide
  by the argument's nearest declaration in the function, the file or (for a
  `member_`) the matching header, so only a real `std::string` is reported;
  an argument whose type is not written down still needs review. They found
  407 sites across the tree. The eight in UltraMail are fixed (see UltraMail's
  changelog), EmailCleaner's were fixed before; the other 399 are recorded in
  `scripts/path_string_baseline.txt` as debt - CI blocks any new one. The run
  also got faster: lines are cleaned once and the patterns compiled once.
