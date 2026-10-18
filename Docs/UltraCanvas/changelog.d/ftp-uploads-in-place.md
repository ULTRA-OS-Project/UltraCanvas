- **UltraCloud uploads to FTP and SFTP by way of a part name, and removes
  what an unfinished one left.** A server that writes an upload into the
  file as it arrives - the new `ProviderCapabilities::partialUploadsRemain`,
  set by the FTP provider - left the part of an upload stopped part way
  under the file's own name, and an upload that replaced a file had
  destroyed the old one with its first byte. `CloudService::Upload` and
  `UploadAndShare` now, for every application (`UltraCloudUploads.h`):
  - write to a hidden part name (`UploadPartName`: ".report.pdf.part") and
    rename it into place once complete (`PutUploadInPlace`); a server that
    will not rename onto an existing file (SFTP, some Windows servers) has
    the old one set aside (`UploadSetAsideName`) and put back if the rename
    fails;
  - go straight to the name for an anonymous FTP login
    (`IsAnonymousFtpLogin`), a name too long to mark, a server that refuses
    the part name, and an account whose server refused a rename -
    remembered for 30 days (`AccountStore::SetUploadsDirect` /
    `UploadsDirect`);
  - keep a `PendingUpload` in the account store (`AddPendingUpload`,
    `ListPendingUploads`, `RemovePendingUpload`; schema versions 2 and 3)
    from before the first byte at a part name, and from the moment the
    server created the file at the file's own name - the FTP provider
    watches the reply to STOR, and looks in the folder first on SFTP, which
    says nothing - so a process that ends during the upload leaves the
    record too.
  Every call on such an account first removes what those records point to,
  where it is still the upload's (`IsLeftByPendingUpload`: any part; at the
  file's own name, one shorter than the whole file), puts back an old file
  left set aside, and says each step in the thread's log. A part a listing
  leaves out (vsftpd hides dot names) is removed by name; one the server
  will not remove stays recorded for the next run; a cleanup the server did
  not answer is tried again straight after a later call that gets through,
  up to `kPendingCleanupTriesPerRun` times a run, and a listing then leaves
  out what was removed. `UltraCloudTests` (`test_uploads.cpp`, 17 cases)
  run it all against a fake FTP server that can refuse renames, deletes or
  dot names, hide dot names, break an upload or not answer.
