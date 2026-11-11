- **The desktop's look, and every text translatable.** UltraPassword is
  light or dark as the desktop is - or as *Look* on UOS-Settings' Appearance
  page says - in the desktop's font and size: the colours and text sizes of
  `ui/Theme.h` are the framework theme's roles now, read when a window is
  built. Every text the windows and dialogs show goes through `Tr`, the
  sentences built from pieces rewritten with placeholders and plurals, and
  UltraPassword's own English set (`media/locale/en/UltraPassword.json`,
  over the Security base set) is extracted from them; UOS-Settings'
  Translations page edits it.
