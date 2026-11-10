- **A copied file keeps its original's dates.** The Filer copied with
  `std::filesystem::copy_file`, which on Windows sets a copy's creation date
  to the moment of the copy and on Linux and macOS keeps no date at all; a
  file over the chunking threshold, copied in pieces for the progress ring,
  and every copied folder lost all of them. New `UltraCanvasFileTimes.h`
  (`CopyFileTimes`, `CanCopyCreationTime`) carries them over: created,
  modified and accessed on Windows (SetFileTime) and macOS (setattrlist +
  utimensat), modified and accessed on Linux and the BSDs (utimensat - the
  kernel lets no program set a creation date), the modification date through
  `std::filesystem` elsewhere. `UltraCanvasFilerWidget` calls it after every
  file and every folder (after its contents) it copies - paste, drop,
  *Duplicate* and a move between drives. `FileTimesTest` checks a file and a
  folder against the platform's backend.
