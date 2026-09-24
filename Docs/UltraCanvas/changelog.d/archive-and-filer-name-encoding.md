- **"Namensänderung" was drawn as "Namens•nderung".** Two ways a name that
  is not UTF-8 reached the file display, and both are closed:
  - *Archives.* A ZIP entry without the UTF-8 flag is named in the DOS code
    page of the machine that made it - IBM437 by the ZIP specification, and
    what Windows Explorer, WinZip and older 7-Zip write, so "ä" is the byte
    0x84. libarchive passes those bytes on untouched on Linux, so the
    VirtualFS listing showed U+FFFD and `ExtractAll` created a folder whose
    name was not UTF-8 at all. The libarchive provider now reads every entry
    name through one helper: libarchive's own UTF-8 conversion when it has
    one, the stored bytes when they already are UTF-8 (Info-ZIP on Linux and
    macOS write those unflagged), and otherwise IBM437 for ZIP and
    Windows-1252 for the other formats. Extraction writes that UTF-8 name to
    disk.
  - *The locale.* libarchive converts names through the C library, so in the
    "C" locale (a test runner, a service, a session without `LANG`) every
    non-ASCII name - Thai, Russian, Chinese, flagged UTF-8 or not - came
    back empty and `archive_read_next_header` answered `ARCHIVE_WARN`, which
    every loop in the provider took for the end of the archive. The provider
    now pins `LC_CTYPE` to UTF-8 for its thread while it reads, and a warning
    no longer ends a walk.
  - *Names already on disk.* A file named in a legacy code page (an old
    Latin-1 tool, an unzip that did not re-encode) is shown decoded by
    `UltraCanvasFilerWidget::DisplayNameOf` instead of as U+FFFD. The new
    `RepairLegacyEncodedName` / `IsWellFormedUtf8` in `UltraCanvasTextUtils.h`
    pick Windows-1252 or IBM437, whichever makes letters of the stray bytes,
    and leave UTF-8 - including decomposed (NFD) names - untouched. The entry
    keeps its real bytes for every file operation. The rename field opens on
    the decoded name, and an edited name is written as UTF-8.
  - New tests: `VirtualFSNameEncodingTest` (a hand-built ZIP with an IBM437
    "Namensänderung/Grüße.txt" and UTF-8 Thai, Russian and Chinese entries,
    listed, read and extracted in the "C" and a UTF-8 locale) and
    `FilerNameEncodingTest` (the repair, `DisplayNameOf`, caption wrapping of
    Thai / Cyrillic / CJK names, and a real folder scan).
