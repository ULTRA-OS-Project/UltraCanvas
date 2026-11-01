- **Every program in a folder showed one program's icon until its own
  loaded - and programs without an icon kept it.** On Windows, under
  Display > File icons = Host OS, the type icon for `.exe` was asked of the
  shell with the path of whichever program was drawn first. For the types
  whose registered icon is `%1` (`.exe`, `.ico`, `.cur`, `.lnk`) the shell
  reads that file even with `SHGFI_USEFILEATTRIBUTES`, so it answered with
  that program's own icon, and the filer held it as *the* `.exe` icon: every
  program drew it while its own icon was still being extracted, and the
  console tools, which carry no icon, drew it for good - until the slot was
  evicted and happened to be asked again by an icon-less program.
  `HostFileIconProbeName` (`UltraCanvasHostFileIcons.h`) gives the made-up
  name the key stands for (`ultracanvas-host-icon-probe.exe`), and the
  Windows backend asks the shell with it, never with the file's path - which
  also stops a special folder's icon from standing in for every folder.
  `FilerHostIconsTest` checks that two programs, a Windows and a POSIX path,
  and any two folders are asked about under one name that carries nothing of
  the file.
