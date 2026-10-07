- **The file dialog says why OK did nothing.** In Open mode, OK on a typed
  name that is not a file was swallowed, so the button read as dead; it now
  shows a **File Not Found** notice naming the name and the folder, and the
  dialog stays open on the name. A Save typed into a folder that does not
  exist gets a **Folder Not Found** notice the same way.
- **`FileDialog.conf` failures are reported, and it is written only when
  something changed.** A settings folder that could not be written left the
  file dialog forgetting its view, size and folder with no trace; the failed
  write, and a file that is there but cannot be read, are now reported once
  per run on `debugOutput`. `FileDialogSettings::Update` compares the
  settings before and after the change and skips the temp-file-and-rename
  when a close changed nothing, which is most closes.
