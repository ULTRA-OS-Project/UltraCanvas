- **The desktop's look, and every text translatable.** UltraMail is light
  or dark as the desktop is - or as *Look* on UOS-Settings' Appearance page
  says - in the desktop's font and size: the colours and text sizes of
  `UltraMailTheme.h` are the framework theme's roles now, read when a
  window is built, so a dark desktop gives a dark UltraMail. Every text the
  windows and dialogs show goes through `Tr`, with the sentences built from
  pieces rewritten with placeholders and plurals; UltraMail's own English
  set (`media/locale/en/UltraMail.json`, over the Communication base set)
  is extracted from them, and *Translate UltraMail...* on the Settings page
  opens the localisation editor. The signature verdicts of the engine stay
  English for now.
