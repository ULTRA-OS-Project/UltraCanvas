- **FTP rename, delete and new folder work in subfolders and on names with
  spaces.** `UltraNet_FtpRename`, `UltraNet_FtpDelete`,
  `UltraNet_FtpCreateDirectory` and `UltraNet_FtpRemoveDirectory` had three
  bugs:
  - The name was cut from the URL still percent-encoded, so
    `RNFR My%20Photo.jpg` asked for a file that does not exist. Any name with
    a space, a bracket, `+`, `&` or a non-ASCII letter failed with a 550.
  - libcurl sends quote commands before it changes into the URL's folder, so
    every command ran in the login folder. A rename in a subfolder failed, a
    new folder was created at the top of the server, and a delete in a
    subfolder could remove a same-named file at the top instead. Commands now
    name the entry by its path from the login folder, as libcurl reads the URL.
  - An `sftp://` URL was sent FTP commands, which SFTP does not speak. It now
    gets libcurl's SFTP commands (`rename`, `rm`, `rmdir`, `mkdir`) with
    quoted full paths.

  A name containing a line break is refused, since on FTP it would start a
  second command. The command text is built in `UltraNetFtpQuote.h`, is
  covered by `UltraNetFtpQuoteTest`, and was checked against a real FTP server.
