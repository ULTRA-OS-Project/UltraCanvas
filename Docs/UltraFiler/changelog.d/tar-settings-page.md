- **Settings > Archives > TAR** sets how Compress packs a `.tar`,
  `.tar.gz`, `.tar.bz2`, `.tar.xz` and `.tar.zst`: the compression level of
  gzip, bzip2, xz and Zstandard (their own defaults - 6, 9, 6, 3 - until
  changed), the threads of xz and Zstandard (every core by default),
  Zstandard's long mode (`--long=27`), the header format (PAX, GNU tar or
  ustar), whether each file keeps its modification time, permissions and
  owner, whether a symbolic link is stored as a link or as what it points
  to, and patterns of names to leave out (`.git`, `*.tmp`, `build/*.o`).
  *Restore defaults* puts them all back.
- **Pointing at an entry of the Compress menu shows what it packs with** -
  the equivalent command line, e.g.
  `tar --format=pax --owner=0 --group=0 --numeric-owner -cf - <files> | xz -6 -T0`,
  and what is kept of each file - so the TAR settings can be read off the
  menu itself.
- Packed files keep their own modification time and permissions (they used
  to carry the time they were packed and 0644) - see the framework
  changelog.
