- **UltraCloud: an upload that was complete when the connection went is put
  in place, not removed.** An upload to an FTP or SFTP server goes to a
  hidden part name and is renamed into place once the last byte is in
  (`UltraCloudUploads.h`); a connection that died between the two left the
  whole file under the part name, and the next call on the account removed
  it like any unfinished upload. Now `CloudService` notes the pending upload
  as complete (`PendingUpload::completedAt`, account-store migration 4) and
  the next call that reaches the server gives it its name
  (`PlaceCompletePart`) - also when the process ended in between and the
  folder lists the part at the file's full size. A file that has the name by
  then is replaced when the upload finished within
  `kCompleteUploadReplacesSeconds` (an hour: it was replacing that file) and
  otherwise kept, the upload put beside it as *name (2).ext*
  (`UploadKeptName`), so a file written there since is never lost; an old
  file set aside for the replace is removed once the new one has its name.
  Only a server that refuses the rename still has the part removed. The
  upload's error says it: *it is given that name the next time this account
  reaches the server*. A step called off by the thread's abort check (the
  application quitting) counts as unanswered, never as a refusal: the
  account does not go over to uploading straight to its names, and nothing
  is sent again. `uploads_a_complete_*` in `Tests/UltraCloud` cover
  each case against the fake FTP server.
- **UltraNet: one per-thread check calls an FTP call off.**
  `UltraNet_SetThreadFtpCancel` is gone; `UltraNet_SetThreadAbortCheck`
  (`UltraNetCore.h`), which the synchronous HTTP requests and the FTP
  transfers already asked, is now asked by every FTP call - listings and
  `DELE`/`RNFR` too - beside the call's own `UltraNetFtpOptions::isCancelled`.
  A thread that wants its calls stopped sets that one check. UltraFiler's
  drive worker folds its quit-time abort and its preview-copy cancel into
  it, which also calls a preview download off on a cloud drive, not only on
  an FTP one. `ftp_log_the_thread_abort_check_calls_off_a_listing` covers it.
