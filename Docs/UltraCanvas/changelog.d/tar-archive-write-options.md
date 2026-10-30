- **How a TAR is packed can be set, and every Compress entry says how it
  packs.** `UltraCanvasFilerWidget::SetTarSettings(FilerTarSettings)` gives
  the TAR entries of the context menu's Compress the level of each
  compressor (gzip, bzip2, xz, Zstandard), the threads of xz and Zstandard
  (`-T0` by default), Zstandard's long mode, the header format (pax, GNU,
  ustar), what is stored of each file, symbolic links, and names to leave
  out of a packed folder. Each entry of the Compress submenu shows the
  parameters it packs with as a tooltip - the equivalent command line, e.g.
  `tar --format=pax --owner=0 --group=0 --numeric-owner -cf - <files> | xz -6 -T0`
  (`DescribeCompressParameters`). Underneath, VirtualFS takes the same
  choices as `VirtualFSOpenOptions::archiveWrite`
  (`VirtualFSArchiveWriteOptions`), and `UCVFSBridge::CreateArchive` as
  `UCVFSCompressionOptions::archive`.
- **Packed files keep their own modification time and permissions.** Every
  entry VirtualFS packed carried the time it was packed and 0644 (folders
  0755), whatever the file had - an unpacked script was no longer
  executable, and every file looked new. A file now keeps its own time and
  permission bits (the owner is still left out unless asked for), a symbolic
  link in a TAR is stored as a link instead of a copy of its target, and
  file content is streamed into the archive instead of being read into
  memory whole. ZIP and 7-Zip store what a link points to, as `zip -r` and
  `7z a` do.
- New test `VirtualFSArchiveWriteOptionsTest`: compressor levels and
  threads, Zstandard long mode, the three header formats, kept and
  normalised times / permissions / owner, links stored and followed, and
  the exclusions.
