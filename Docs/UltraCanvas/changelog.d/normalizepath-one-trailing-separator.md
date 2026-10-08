- **`NormalizePath` doubled the separator at the end of a folder on Windows.**
  `NormalizePath(GetResourcesDir() + "media/icons/")` came back ending in
  `\\`, and every file name appended to it carried the doubled backslash -
  `GetBundledFontsDir()`, the file display's icon folders and the demo apps'
  media folders among them. A folder given with `/` or `\` at its end now
  ends in exactly one native separator on every platform (the root `/` on
  Linux and macOS, which came back as `//`, included).
- **On Linux and macOS a path that did not exist could come back cut
  short.** `realpath` fails for it, and POSIX leaves its buffer undefined
  then; glibc leaves the path up to the first name that is missing, so a
  file in a folder that did not exist (`.../missing/sub/file.txt`) came back
  as that folder (`.../missing`). Such a path is now returned as it was
  given, so whatever opens it next names the file it did not find.
- `FilerNameEncodingTest` checks both, on the Windows CI row too: a
  Thai-named folder asked for with `/` or `\` ends in one separator and a
  file name appended to it reaches the file, the root keeps one separator,
  and a missing path comes back whole.
