- **UltraCloud: a download can continue a partial copy**
  (`SetThreadDownloadContinues` / `DownloadContinues`, UltraCloudProvider
  0.4.0). While the calling thread says so, `Download` treats a file already
  at the target path as the first part of the remote file and asks only for
  the rest.
  - FTP (UltraCloudFtp 0.7.0): UltraNet's resume - REST at the partial size,
    the rest appended. A server that refuses the REST (a Server-class
    failure) is asked again for the whole file.
  - HTTP (`HttpProviderBase::DownloadToFile`, UltraCloudHttp 0.7.0): a Range
    request (`resumeFromOffset`). 206 appends; 200 - the server ignored the
    range - replaces the file; a 416 naming the partial size means it was
    complete; a 416 for another size fetches the file again from the start.
  - A download that fails while continuing leaves what has arrived, for the
    next try; the caller decides when a partial copy is thrown away.
  - A thread setting rather than a parameter, so `ICloudProvider`, which
    provider plug-ins are built against, keeps its layout; a provider that
    does not read it downloads the whole file, which is still correct.
  - Tests: six continuing cases in `test_downloads.cpp`, and
    `ftp_a_continuing_download_resumes_at_the_partial_size` in
    `test_ftp.cpp`.
