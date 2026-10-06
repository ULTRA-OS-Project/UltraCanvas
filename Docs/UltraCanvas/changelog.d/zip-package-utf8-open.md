- **ZIP packages open by their UTF-8 name, and without locking the file.**
  `UCZipPackageReader::Open` - behind the CorelDRAW thumbnails and the
  ODT / DOCX / mind-map imports - handed the path to miniz's own fopen, which
  turns a UTF-8 name into UTF-16 only under MSVC and 64-bit MinGW; with any
  other Windows toolchain a Thai, Cyrillic or emoji name went through the ANSI
  code page and the file did not open. Where miniz did convert the name, it
  opened with `_wfopen_s`, which denies every other program write access while
  the archive is open, so saving a drawing in CorelDRAW could fail while the
  file display was reading its thumbnail. The reader now opens the file
  itself through `OpenFileUtf8` (`_wfopen`, shared like every other open) and
  closes it in `Close()`. A file that cannot be opened now says so ("Cannot
  open file") instead of "Not a valid ZIP archive". The writer already opened
  its file this way.
  - New `ZipPackageTest` writes and reads a package in a Thai-and-emoji
    folder, and on Windows writes the file from a second handle while the
    reader holds it. Windows CI compiles and runs it by hand next to
    `PathUtf8Test`, on its code page 1252 runner.
