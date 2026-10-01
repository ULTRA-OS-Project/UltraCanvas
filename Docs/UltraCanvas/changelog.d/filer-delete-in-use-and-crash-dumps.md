- **A Windows crash now leaves a dump behind, and the crash message names
  it.** The unhandled-exception filter writes a minidump - every thread's
  stack, the module list, the memory the stacks refer to and the modules'
  globals - to `%LOCALAPPDATA%\UltraCanvas\CrashDumps\<app>-<date>-<time>-<pid>.dmp`
  before it shows the message box, and the box (and the log line after the
  crash line) says where it is and to attach it to the bug report. The
  writer is `MiniDumpWriteDump` from the `dbghelp.dll` every Windows ships,
  resolved at startup since a crash handler can load nothing; the folder is
  created then too. `ULTRACANVAS_CRASH_DUMP_DIR` names another folder,
  `ULTRACANVAS_NO_CRASH_DUMP=1` writes none. Until now a crash on a user's
  machine left the exception code and the faulting module and nothing else;
  Windows itself keeps no dump for a desktop program unless a registry key
  asks for one. Documented in *UltraCanvasWindowsDiagnostics.md*, with how
  to open a dump and the registry key for Windows' own.
- **The Filer no longer asks for administrator rights to delete a file a
  running program holds, names the file that stopped a folder's delete, and
  refuses to delete the application it is running from.** Deleting an
  unpacked download from the UltraFiler that had been started out of it
  asked *administrator permission needed* for `Resources` and `lib`, deleted
  everything else, and crashed: the files the running program had loaded
  answered Windows' "access denied", which the dialog took for a permission
  problem, and the fonts, icons and plugins it had not loaded yet went,
  after which the program fell over the first one it reached for.
  - A failure inside a folder is reported for the **file that refused**
    (`"libvips-42.dll" in "lib" could not be deleted.`, with a *Stopped at:*
    line under the folder's path), not for the folder, which still had the
    rest of its content and looked deletable.
  - Before deciding that "access denied" means administrator rights, the
    worker asks the lock probe (the Restart Manager) whether a program holds
    the file. One that is held gets a **Delete: a file in the folder is in
    use** dialog naming the program (*In use by: UltraFiler (4120)*), with
    Skip / Try again and no administrator button: a loaded program file
    cannot be deleted by anyone, and the consent prompt only cost a click.
    When the holder is the running application itself the note says to
    close it and delete from elsewhere.
  - A delete that would take the running application apart - a victim at or
    below the folder the executable runs from, or a folder holding it - is
    **refused up front**, with *Cannot delete: UltraFiler is running from
    here*, and nothing is touched.
  - A **read-only file inside a folder** no longer stops the delete with
    "access denied": it is lifted and removed, as *Delete anyway* lifts the
    entry's own protection, so a folder unpacked with its read-only bits goes
    in one pass and without a question about rights it never needed.
  - The delete queue converts its paths with `PathFromUtf8` throughout
    (`RemoveTreeWithProgress`, `CountTreeEntries`, the elevated finish): the
    implicit `fs::path(std::string)` conversions named a different file for a
    non-ASCII name on a Windows before 1903.
- **A modal dialog widens to its footer, and its buttons take the style's
  font size.** A footer row that needs more than the configured width - the
  Filer's *Apply to all later permission failures* checkbox beside *Delete as
  administrator / Try again / Skip / Stop* - ran off the right edge and the
  last button was gone. `AutoSizeToContent` now measures the row (its
  padding, every element and button, the gaps) and widens the window to it,
  up to 90% of the monitor, before fitting the height to the text at the new
  width. `ModalDialogStyle::buttonFontSize` (0 = the button's default) sets
  the footer buttons' label size, applied after the role style and
  re-applied by `SetStyle`.
- **The Filer's operation dialogs read at the display's font size.** The
  delete / copy / conflict / problem / summary dialogs set the message and
  the buttons to `FilerStyle::fontSize` and the details, the note, the
  *Apply to all* checkbox and the entry list to `smallFontSize`, so a host
  running its UI at 9 (UltraFiler) gets dialogs at 9 instead of the dialog's
  default 12 over a window of smaller text; the same sizes go on the *Cannot
  Delete* and *Cannot delete: … is running from here* dialogs.
