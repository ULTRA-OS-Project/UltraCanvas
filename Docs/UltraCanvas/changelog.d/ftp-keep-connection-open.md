- **UltraNet FTP keeps a connection open for the next call, and stops asking
  a server for MLSD once it has refused it.** Every FTP call used to make its
  own libcurl handle and close the connection with it, so browsing a server
  was a full connect and login per folder - and on a server without MLSD
  (vsftpd answers "500 Unknown command."), two per folder: one to be refused
  MLSD, one more to ask with LIST. A UltraFiler session opening one folder
  and reading four subfolders ahead logged in ten times. Now each thread's
  calls share a libcurl connection pool (`UltraNetFtp.cpp`,
  `ThreadConnections`), so a call to the same server and user takes up the
  connection the last one left open: the LIST fallback runs on the
  connection MLSD was refused on, and the next folder starts at CWD. A
  server that answers MLSD with 500 / 502 / 504 is remembered for the life
  of the process and listed with LIST straight away; a 550 (the folder
  refused, not the command) is not taken as that. The same five listings
  against vsftpd 3.0.5 are one login, one MLSD and five LISTs.
  - Changes are the exception: on FTP, `UltraNet_FtpDelete`, `FtpRename`,
    `FtpCreateDirectory` and `FtpRemoveDirectory` still log in on a
    connection of their own and close it. libcurl sends their commands
    (DELE, RNFR / RNTO, MKD, RMD) before it changes folder, and they name
    the entry from the folder a login lands in; on a kept connection that a
    listing left in /photos/, "DELE photos/a.txt" named
    /photos/photos/a.txt. SFTP's commands carry the full path and may use a
    kept connection.
  - New `UltraNet_FtpCloseIdleConnections()` closes the calling thread's
    open connections; a thread's are also closed when it ends, and every
    thread's by `UltraNet_Shutdown`. One pool per thread because libcurl
    does not support sharing connections between threads that transfer at
    the same time.
  - The session log says so: *Using the open connection to host - already
    logged in* instead of resolving and logging in, and *Connection kept
    open for the next request* for libcurl's "left intact". "Resolving
    address of" is now held back until libcurl says what it does first.
  - Tests: `test_ftp_log.cpp` counts logins and MLSD requests against the
    scripted loopback server (three listings, one login, one MLSD; a 550
    refusal leaves MLSD on; a close logs in afresh; a delete after a
    listing in a subfolder names the right file, on a login of its own) and
    checks the reuse wording of libcurl 8.21 and earlier. The scripted
    server now keeps each connection's folder and serves connections side
    by side. `UltraNetApiStatus` probes the new
    function, against a real server when `ULTRANET_PROBE_FTP_URL` is set.
