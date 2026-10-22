- **A file that cannot be saved says so, and why.** When writing a file
  failed - the save icon, Ctrl+S, Save As, or *Yes* to "Save all before
  closing?" - nothing happened: the tab stayed unsaved, closing the window
  just stopped, and no message appeared. Texter now shows a *Save Failed*
  message with the file's full path, the step that failed (opening the file
  or writing it), the system's error code and text (for example *13:
  Permission denied*), whether the folder still exists or the file is marked
  read-only, and a hint at the usual causes. It then opens the Save dialog in
  the file's folder, and again after each failed attempt. When closing,
  cancelling that dialog keeps the window open with the edits; a file saved
  elsewhere lets the close go on.
