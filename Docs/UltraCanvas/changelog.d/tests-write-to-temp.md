- **`VirtualFSDeleteTest` and `WordFormatsTest` no longer write into the
  source tree.** Run without an argument, both wrote their files into the
  current directory: a run from the repository root left
  `VirtualFSDeleteTest`'s archives in `vfsdelete-test-out/`, where they were
  committed (`bulk.zip`, `bulk.tar.gz`, `manager.zip`) and every later run
  rewrote tracked files, and `WordFormatsTest`'s some fifty sample documents
  (`sample.odt`, `edited.docx`, `mdmedia/`, ...) loose in the root. Both now
  default to a folder in the system temp directory (ctest still passes one in
  the build tree), the three archives are removed from the repository, and
  `.gitignore` keeps `vfsdelete-test-out/` out should an older build of the
  test still write it.
