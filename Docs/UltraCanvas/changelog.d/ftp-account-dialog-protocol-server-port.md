- **UltraCloud's add-account dialog asks an FTP / SFTP server for its
  protocol, its name and its port, each on its own.** The one *Server URL*
  field took whatever was pasted into it - an `https://` address from the
  browser's bar, which is no FTP at all - and tried it as plain FTP on port
  21 without a word; the port had no place but a `host:port` nobody was
  told about. The FTP provider now shows a *Protocol* menu (SFTP, FTP with
  TLS, FTP over TLS, plain FTP, each with its port named), a *Server* field
  for the name or address and a *Port* field beside it, empty for the
  protocol's own; a URL typed into the server field is refused with the
  reason. The three are stored as the one URL the command line takes
  (`FtpServerUrl`: `sftp://files.example.org:2222`, the protocol's own port
  left out, so an account keeps its id). The *Private key* row, shown for
  SFTP only, has a *Choose* button that opens the file dialog in `~/.ssh`
  with hidden files shown; a `~/` typed into it is expanded. The status
  line wraps onto two lines, so a server's refusal is read whole, and a
  refusal without words of its own still says that much. The callbacks the
  widgets hold reach the other widgets through plain pointers - the dialog
  owns them all and outlives every callback - where the provider menu's own
  callback used to hold a `shared_ptr` to the menu, a cycle that kept the
  menu alive. Its texts go through `Tr`: `extract_ui_strings.py` reads
  `UltraCloud/ui` into the standard set, since the dialog is every
  application's, and the 42 entries come drafted in all thirty languages;
  *Port*, *Protocol*, *User*, *Server* and *Add account* move up into the
  standard set from the Computer and Communication base sets, which kept
  the translations they had (`UltraCloudAccountDialog.cpp` 0.8.0).
- **The FTP provider refuses a scheme it does not speak.** `Verify` answers
  `InvalidArgument` - "the server URL starts with https://, which is not
  FTP: it has to start with ftp://, ftps://, ftpes:// or sftp://" - before
  anything is sent, so `ultrapress hosts add` and UltraFiler's "+ Drive" say
  so too. `FtpServer`, `FtpServerOf`, `FtpServerUrl`, `FtpDefaultPort` and
  `FtpSchemeKnown` in `UltraCloudFtp.h` (0.2.0) are the pure helpers behind
  the form, tested in `Tests/UltraCloud/test_ftp.cpp`.
