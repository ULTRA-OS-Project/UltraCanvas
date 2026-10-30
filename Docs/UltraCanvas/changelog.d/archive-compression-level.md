- **`VirtualFSOpenOptions::compressionLevel` is honoured when an archive is
  written.** It was ignored: every ZIP, 7-Zip and compressed TAR was written
  at the library's default level whatever was asked (and so was
  `UCVFSCompressionOptions::level` through `UCVFSBridge::CreateArchive`).
  ZIP and 7-Zip now take it as their 0-9 level - `Store` stores them
  uncompressed - and the stream compressor of a `.tar.gz` / `.bz2` / `.xz` /
  `.zst` / `.lz4` on its own scale (Zstandard 1 / 2 / 9 / 19 / 22 for
  Fastest / Fast / Good / Best / Ultra). `Normal`, the default, still leaves
  every library at its own default, so no archive written today changes;
  `archiveWrite.filterLevel` still overrides it for the stream compressor.
  `VirtualFSArchiveWriteOptionsTest` checks Fastest against Best for each
  format, a stored ZIP, and that Normal matches the default level.
