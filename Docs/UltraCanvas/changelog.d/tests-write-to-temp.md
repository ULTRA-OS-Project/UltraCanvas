- **Tests no longer write into the source tree.** Run without an argument,
  four tests wrote their files into the current directory, so a run from the
  repository root put them in the source tree: `VirtualFSDeleteTest`'s
  archives in `vfsdelete-test-out/`, where they were committed (`bulk.zip`,
  `bulk.tar.gz`, `manager.zip`) and every later run rewrote tracked files;
  `WordFormatsTest`'s some fifty sample documents (`sample.odt`,
  `edited.docx`, `mdmedia/`, ...) and `LaTeXDocumentTest`'s files loose in
  the root; and `VirtualFSNameEncodingTest`'s archive and extracted folders
  in `vfs-name-encoding-test-out/`, a folder it also deletes on start. All
  four now default to a folder in the system temp directory (ctest still
  passes one in the build tree), the three archives are removed from the
  repository, and `.gitignore` keeps `vfsdelete-test-out/` out should an
  older build of the test still write it.
  - `LaTeXDocumentTest` also finds the shipped `media/LaTex` corpus from any
    directory: its path is compiled in (`LATEXTEST_CORPUS_DIR`) instead of
    the `../../media/LaTex` that resolved only from `build/bin`. Run from
    anywhere else, the corpus checks were skipped and the test still reported
    every check passed; a corpus that cannot be found is now a failure.
