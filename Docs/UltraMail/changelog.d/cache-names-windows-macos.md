- **Folders named like a Windows device, or ending in a dot or space, can have
  their messages downloaded on Windows.** Windows refuses a directory called
  `CON`, `NUL`, `AUX`, `PRN`, `COM1`-`COM9` or `LPT1`-`LPT9` (also before a
  dot, as in "Nul.txt"), and drops a trailing dot or space, so such a folder's
  messages could not be cached there. Their cache directories now carry a
  short `~` code in that place ("Notes." is cached as `Notes~t`, "Con" as
  `C~zon`).
- **Two folders whose names are spelled in different Unicode forms no longer
  share their downloaded messages on macOS.** macOS takes "ü" written as one
  character and "u" followed by a combining "¨" as the same name. A character
  that can make such a second spelling (combining accents, Hangul jamo,
  polytonic Greek, the Ohm and Kelvin signs and a few more) is written in the
  cache directory as `~x` and its code, so each spelling has its own directory.
  The first start moves the messages of folders whose directory changes.
