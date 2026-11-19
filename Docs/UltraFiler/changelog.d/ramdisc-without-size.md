- **A RAM disc on Windows without ImDisk is no longer asked for a size.**
  Without the ImDisk driver the "RAM disc" is a folder on the system drive,
  wiped when it is ejected. The size chosen in the *New RAM disc* dialog was
  never kept to - the folder holds whatever the drive has room for - and the
  disc's card on the Computer page showed the system drive's pie (the same
  69 % and free space as C:), which read as a disc as large as the drive. The
  dialog now asks for the name alone there and says the disc has no size of
  its own; the card shows a neutral ring with *Folder on the system drive* and
  *No size of its own*. With ImDisk installed, on macOS and on Linux the size
  is asked as before.
