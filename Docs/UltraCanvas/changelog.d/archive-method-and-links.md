- **`VirtualFSOpenOptions::compressionMethod` and `followSymlinks` do what
  they say.** Neither was read anywhere.
  - `compressionMethod` now chooses what a ZIP or 7-Zip is written with:
    Store, Deflate, BZip2, LZMA, LZMA2 / XZ and PPMd for 7-Zip; Store and
    Deflate for ZIP (and BZip2, LZMA, XZ and Zstandard where libarchive is
    3.8 or newer). A method this build cannot write - and, for a TAR, any
    method other than the one its extension names - makes `CreateArchive`
    answer `NotSupported` without leaving a file, instead of writing
    something else under the name. `Auto`, the default, is unchanged.
  - `followSymlinks` (on by default) makes reading an archive follow the
    symbolic links inside it, the way a disk does: reading a link reads its
    target instead of nothing, listing a link to a folder lists that folder,
    and a path may pass through linked folders. A link pointing out of the
    archive (absolute, or above its root), a dangling one and a circle are
    never followed. Writing keeps `archiveWrite.storeSymlinks`.
  - `VirtualFSArchiveWriteOptionsTest` covers every method on ZIP, 7-Zip
    and TAR and each kind of link.
- **`UCVFSBridge::CreateArchive` takes the archive's method from
  `UCVFSCompressionOptions::archive.method`** (`UCVFSArchiveWriteOptions`;
  empty: the library's default). `UCVFSCompressionOptions::type` stays the
  buffer compressors' choice and is said to be: passed on to an archive,
  its default (Zstd) would have made every ZIP created through the bridge
  fail before libarchive 3.8. A cabinet method (LZX, MS-ZIP, Quantum), which
  nothing writes, fails with a reason in `GetLastError()`. New test
  `VirtualFSBridgeArchiveTest`.
