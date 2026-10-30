- **A `.7z` or `.tar.xz` showed a blank page on Windows.** With host icons
  on (`FilerFileIconStyle::HostOperatingSystem`), a type no program is
  registered for - `.7z`, `.tar`, `.tar.gz`, `.tar.bz2`, `.tar.xz`,
  `.tar.zst` on a Windows without 7-Zip - was drawn with the shell's blank
  "no program" page, which says nothing about the file. The Windows backend
  of `LoadHostFileIconPixmap` now answers such a type with no icon, and the
  file display draws its own archive icon with the extension on it; a
  registered type keeps Explorer's icon. `FilerHostIconsTest` checks it on
  Windows.
