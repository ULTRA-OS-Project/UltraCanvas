- **A file copied to or from a drive, or out of an archive, keeps its dates.**
  An upload was dated the moment it arrived and a download the moment it was
  written, on every provider; a file taken out of an archive with
  `ExtractFile` was dated now; and a ZIP's or 7z's creation date was dropped
  on unpacking.
  - `UltraCanvasFileTimes.h` gains `FileTimes`, `GetFileTimes` and
    `SetFileTimes` (nanoseconds since 1970, 0 = unknown / leave alone) beside
    `CopyFileTimes`; the std::filesystem fallback converts exactly (libc++'s
    file clock is the Unix epoch, libstdc++ and MSVC convert).
  - UltraNet FTP / SFTP: `UltraNetFtpOptions::setRemoteModificationTime`
    dates an uploaded file (`MFMT`, SFTP `mtime`, sent with libcurl's `*` so a
    server that refuses still has the file), `onRemoteModificationTime`
    reports the server's date of a download (`CURLOPT_FILETIME`: `MDTM` /
    stat). `UltraNetFtpQuote.h`'s `BuildSetModificationTime` makes the
    command.
  - UltraCloud: every provider sends the local file's dates with an upload
    and dates a download with the server's - FTP / SFTP (modified), WebDAV and
    Nextcloud (`X-OC-Mtime` / `X-OC-CTime`; `Last-Modified`), Dropbox
    (`client_modified`), Google Drive (`modifiedTime` / `createdTime`) and
    OneDrive (`fileSystemInfo`), the last two with the creation date too.
    `HttpProviderBase::DownloadToFile` hands back the answer's headers
    (`answered`).
  - VirtualFS: `ExtractFile` writes through libarchive's disk writer (the
    entry's dates and, when asked, permissions); `ExtractAll` follows
    `preserveTimestamps`; a ZIP's or 7z's creation date - read by libarchive
    as the change time - is restored as the creation date on Windows and
    macOS, and stored when packing a ZIP or 7z.
  - Tests: `test_file_dates.cpp` (UltraCloud, every provider),
    `VirtualFSExtractDatesTest`, `UltraNetFtpQuoteTest` (the dating command),
    `FileTimesTest` (get / set).
