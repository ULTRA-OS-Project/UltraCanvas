- **UltraNet: an FTP change works on a server that takes one connection at a
  time.** `UltraNet_FtpDelete`, `UltraNet_FtpRename`,
  `UltraNet_FtpCreateDirectory` and `UltraNet_FtpRemoveDirectory` log in on a
  connection of their own (their commands name the entry from the folder a
  login lands in), so a server that allows one connection per address or user
  - vsftpd's `max_per_ip`, ProFTPD's `MaxClientsPerHost` - turned them away
  while the calling thread kept a connection open from an earlier call: *421
  There are too many connections from your internet address.* Turned away so
  (421, or a 530 saying the maximum number of clients is reached -
  `ftplog::TooManyConnectionsReply`) before any of its commands was sent, a
  change now closes the thread's kept connection and is sent again: at once,
  then after 0.3 s and 1 s if the server is still counting the closed one;
  the waits end early when the call is called off. The log says *The server
  takes one connection at a time: closing the one kept open and trying
  again*. Four tests in `test_ftp_log.cpp` cover it against a scripted server
  that turns a second session away at the greeting or the login.
