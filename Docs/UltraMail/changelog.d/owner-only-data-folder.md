- **The data folder is yours alone.** UltraMail makes its data folder
  owner-only at every start (`SecureDataDir`), before the instance lock is
  written into it: the folder and the folders in it `rwx------`, the files
  directly in it `rw-------`. An earlier version created it with the
  system's default, which on many Linux systems let every other user of the
  computer read the mail, the databases and the vault; that folder is
  tightened the first time. Windows' per-user `%APPDATA%` was already
  private and is left as it is.
