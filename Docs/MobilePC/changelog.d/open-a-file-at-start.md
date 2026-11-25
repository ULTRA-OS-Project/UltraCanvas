- **`--open <id> <file>`: start an application with a file.** Like
  `--launch`, with a file for the application to open; a relative path is
  taken from the app's own folder, where the media MobilePC carries are
  unpacked. ArtCreator says in the log whether it read the file and how many
  objects the drawing has, and the emulator test now opens an SVG in
  ArtCreator on the second display and fails unless it was read - so
  drawings opening on Android is checked on every run, not assumed.
