- **UltraNet: an FTP change uses the connection kept open.**
  `UltraNet_FtpDelete`, `UltraNet_FtpRename`, `UltraNet_FtpCreateDirectory`
  and `UltraNet_FtpRemoveDirectory` sent their commands before libcurl changed
  folder (`CURLOPT_QUOTE`), naming the entry from the folder a login lands in,
  so each needed a connection of its own: a second login per change, and on a
  server that takes one connection per address a refusal that the previous
  release had to work around by closing the kept connection. Now on FTP the
  commands go once libcurl has changed into the entry's folder
  (`CURLOPT_POSTQUOTE`, `ftpquote::Plan::afterFolderChange`) and name the
  entry within it; on a kept connection libcurl goes back to that
  connection's login folder first (it records it per connection), so the
  change takes the kept connection up like any transfer - one login for a
  listing and the changes after it, and no second connection asked of a
  one-connection server. SFTP's commands, with full paths, are as they were.
  - The "too many connections" recovery moves from the changes into every FTP
    call: a call that needs a new connection all the same - another account
    on the same server, which libcurl does not take up for this one - and is
    turned away at the greeting or the login (`ftplog::TooManyConnectionsReply`),
    before anything was asked and with no byte moved, closes the thread's kept
    connection (`ThreadConnections::UsedBefore`: one an earlier call used) and
    is made again, at once and after 0.3 s and 1 s. Listings and transfers too,
    so two accounts on such a server can take turns on one thread.
  - `test_ftp_log.cpp`: a delete and a rename elsewhere on the kept connection
    name the right files on one login; a one-connection server is not asked
    for a second; another account's change and the listing after it get in.
    The scripted server does RNFR / RNTO; `UltraNetFtpQuoteTest` checks the
    bare names.
