- **`VirtualFSDeleteTest` no longer writes into the source tree.** Run without
  an argument it put its archives in `vfsdelete-test-out/` under the current
  directory, and a run from the repository root had them committed
  (`bulk.zip`, `bulk.tar.gz`, `manager.zip`); every later run there rewrote
  tracked files. It now defaults to the system temp directory (ctest still
  passes a directory in the build tree), the three archives are removed from
  the repository, and `.gitignore` keeps the folder out should an older
  build of the test still write it.
