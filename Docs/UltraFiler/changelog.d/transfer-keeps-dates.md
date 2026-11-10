- **Files copied to or from a remote drive, or extracted from an archive, keep
  their dates.** An upload to an FTP, SFTP, WebDAV, Nextcloud, Dropbox,
  Google Drive or OneDrive drive was dated the moment it arrived, and a file
  copied off one the moment it landed. Now an upload keeps the file's
  modified date (and its created date on Nextcloud, Google Drive and OneDrive)
  and a download gets the date the server holds. *Extract* gives an
  unpacked file its date from the archive and, from a ZIP or 7z, its created
  date as well on Windows and macOS.
