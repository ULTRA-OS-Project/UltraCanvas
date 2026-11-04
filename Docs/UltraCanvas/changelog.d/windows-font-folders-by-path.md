- **Windows: the system fonts could be missing for the whole session.** A
  program showed and offered only its bundled fonts (Ubuntu, Noto Color
  Emoji) and the generic Sans, Serif and Monospace. The generated fontconfig
  set-up named the Windows font folders through fontconfig's keywords
  (`WINDOWSFONTDIR`, `WINDOWSUSERFONTDIR`, `LOCAL_APPDATA_FONTCONFIG_CACHE`);
  how fontconfig resolves them in a configuration read from memory differs
  between versions, and one it could not resolve failed the whole
  configuration, and with it the scan of the system fonts. And
  `FONTCONFIG_FILE` kept naming the start's file of bundled fonts alone, so
  any reload of fontconfig's configuration dropped every system font.
  - The folders are named by path (`GenerateWindowsFontFoldersConfig()`,
    `GetWindowsFontsFolder()`), from `%SystemRoot%` and `%LOCALAPPDATA%`.
  - Once the full set is in use, `FONTCONFIG_FILE` names its `fonts.conf`.
  - A font set with folders of its own but not Windows' (a `fonts.conf`
    found beside the program) gets them added.
  - The debug log says whether the Windows font folder is read.
