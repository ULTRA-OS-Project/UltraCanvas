- **UltraCloud: cloud downloads go to disk as they arrive** (UltraCloudHttp
  0.6.0, `HttpProviderBase::DownloadToFile`). Dropbox, OneDrive, Google Drive
  and WebDAV / Nextcloud downloads held the whole file in memory and wrote it
  afterwards, so every download cost its own size in memory and a file larger
  than the process could spare could not be fetched at all. They also ran
  under `Send`'s two-minute whole-transfer limit, which a large file on a
  slow line could not meet. The body of a 2xx answer is now written chunk by
  chunk (UltraNet's `onDataChunk`), with no whole-transfer limit and a
  30-second stall limit instead. A refusal's body is still kept for its
  reason, so Google Drive's rate-limit 403 is still recognised and waited
  out, and nothing is written for it. A download that fails part-way - the
  connection lost, the caller calling it off, the disk full - removes what
  it wrote. `test_downloads.cpp`: streamed, refused, lost part-way, a file
  that cannot be made, throttled then answered, and a body handed over whole.
