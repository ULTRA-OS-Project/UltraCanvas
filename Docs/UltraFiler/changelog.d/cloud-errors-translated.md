- **A failed cloud operation or listing says why in your language.** A
  download, upload, delete or rename that fails on a remote drive, and a
  folder that cannot be listed, used to show only the provider's English
  text ("cannot list this folder: 530 Login incorrect"). They now lead with
  a sentence in the reader's language picked by the failure's kind -
  sign-in refused, server unreachable, not found, the server's error with
  its HTTP status, rate limited - through UltraCloud's `DescribeResult`,
  and keep the provider's own words in brackets, since "550 Permission
  denied" still tells the user what to change
  (`UltraFilerRemoteDrives.cpp` 1.14.2).
