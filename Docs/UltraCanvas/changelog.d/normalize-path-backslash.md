- **`NormalizePath` no longer puts a `/` after a file name ending in `\` on
  Linux and macOS.** A trailing `\` counted as a folder's separator on every
  system, so a file named `a\` came back as `a\/`, a path that reaches
  nothing. Outside Windows a `\` is part of the name and the path comes back
  as it is; on Windows it still ends a folder. `FilerNameEncodingTest`
  checks both a file that exists and one that does not.
