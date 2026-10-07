- **ZIP files open by their UTF-8 name, and without locking the file.** Three
  readers handed the path to miniz's own fopen: `UCZipPackageReader::Open`
  (the CorelDRAW thumbnails, the ODT / DOCX / mind-map imports), the ODS
  spreadsheet loader, and VirtualFS's fast ZIP rewrite behind deleting entries
  from an archive. miniz turns a UTF-8 name into UTF-16 only under MSVC and
  64-bit MinGW; with any other Windows toolchain a Thai, Cyrillic or emoji
  name went through the ANSI code page and the file did not open. Where miniz
  did convert the name, it opened with `_wfopen_s`, which denies every other
  program write access while the file is open, so saving a drawing in
  CorelDRAW could fail while the file display was reading its thumbnail. All
  three now open the file themselves through `OpenFileUtf8` (`_wfopen`,
  shared like every other open), hand it to miniz, and close it when done.
  The package writer and the ODS writer already opened their files this way.
  - `UCZipPackageReader`: a file that cannot be opened now says so ("Cannot
    open file") instead of "Not a valid ZIP archive".
  - VirtualFS: the raw-copy rewrite now also opens the temporary archive it
    writes through `OpenFileUtf8`, and fails the delete when closing that file
    fails. miniz closed it itself and ignored the answer, so a disk that
    filled up during the last flush left a cut-off archive that was then
    renamed over the original. `VirtualFSLibArchiveProvider` 1.3.1.
  - Tests: the new `ZipPackageTest` writes and reads a package in a
    Thai-and-emoji folder, and on Windows writes the file from a second handle
    while the reader holds it; Windows CI compiles and runs it by hand next to
    `PathUtf8Test`, on its code page 1252 runner. The new
    `SpreadsheetOdsFileTest` saves and reloads a sheet under a Thai-and-emoji
    name, and `VirtualFSDeleteTest` deletes entries from a ZIP with one.
