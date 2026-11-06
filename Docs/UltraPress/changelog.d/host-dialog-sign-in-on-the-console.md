- **Host… reports the sign-in on the console.** Adding a host that refused
  - or answered nothing, as a web server does to an FTP login - showed one
  line in the dialog and nothing anywhere else. The sign-in the dialog runs
  is now written to the console as it happens: where it signs in and as
  whom, the steps and the FTP session's commands and replies, and how it
  ended with the transport's diagnostics, the way `ultrapress hosts add`
  reports one (`UltraPressWindow.cpp` 0.8.7). The dialog itself asks an FTP
  / SFTP host for its protocol, its name and its port each on its own, and
  chooses the private key with a file dialog - the framework's change of
  the same day.
- **`ultrapress hosts add --port <n>`.** An ftp host's port, when it is not
  the protocol's own, as the window's dialog asks for it; it goes into the
  stored URL, so `--port 2222` and `sftp://my.example:2222` are the same
  host. `hosts add` also refuses a server URL that is not FTP - `https://`,
  pasted from the browser - itself, so `--no-verify` stores none
  (`UltraPressCli.cpp` 0.7.2).
