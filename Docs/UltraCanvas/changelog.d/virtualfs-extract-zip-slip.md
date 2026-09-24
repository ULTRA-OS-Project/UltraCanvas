- **Extracting an archive could write files outside the destination ("zip
  slip").** `VirtualFSLibArchiveProvider::ExtractAll` joined every entry path
  to the destination as it was and set none of libarchive's secure-extract
  flags, so an entry named `../../.bashrc` landed outside the folder the user
  picked, and an archive that first extracted `link -> /etc` could then write
  `link/passwd` through it. UltraFiler's Extract reaches this with any archive
  the user unpacks.
  - Every entry path is checked before it is joined: an absolute name (and on
    Windows a drive letter or a UNC path) or any `..` component is refused.
    The entry is skipped, the rest of the archive still extracts, and the
    refused names are listed in `lastError`; the result is `InvalidPath`
    instead of `Success`.
  - libarchive's own guards run behind that check:
    `ARCHIVE_EXTRACT_SECURE_NODOTDOT` and `ARCHIVE_EXTRACT_SECURE_SYMLINKS`
    (no write through a symbolic link on disk). `SECURE_NOABSOLUTEPATHS`
    cannot apply, because the path handed over is always the absolute
    destination plus the entry. The destination's own symbolic links (macOS
    `/tmp` → `/private/tmp`) are resolved first, so only links the archive
    put there count.
  - Hard links: the target stayed relative to the archive root and resolved
    against the process's working directory. It is now held to the same rule
    and prefixed with the destination like every other path.
  - `ARCHIVE_WARN` from `archive_write_header` (an owner that could not be
    restored) no longer aborts the whole extraction, and an entry libarchive
    refuses (`ARCHIVE_FAILED`) is skipped and reported, not the end of the
    walk; the result is then `WriteError`.
  - The skipped entries reach the user. `VirtualFSManager::ExtractAll`,
    `VirtualFS_ExtractAll` and `UCVFSBridge::ExtractArchive` take an optional
    `std::string* outError` with the provider's own account: a heading line
    ending in ':' per kind of problem, then one entry per line, with the
    destination prefix trimmed from libarchive's reasons. It is an
    out-parameter rather than a shared "last error" because extractions run on
    worker threads. `UltraCanvasFilerWidget` shows it as an **Extraction
    Incomplete** dialog that lists the entries, and puts one sentence in the
    status line; the bare "Extraction failed for X" is left only for an
    archive that could not be extracted at all, and now carries the reason.
  - New test `VirtualFSExtractSafetyTest`: a hostile ZIP (`../escape.txt`,
    an absolute name, `a/../../escape2.txt`, a link out followed by a file
    through it) and a tar with a good and a climbing hard link, extracted from
    another working directory, plus a destination reached through a symbolic
    link. The hand-written ZIP builder is shared with
    `VirtualFSNameEncodingTest` as `Tests/VirtualFSTestZip.h`.
