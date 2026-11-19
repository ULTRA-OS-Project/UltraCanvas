- **Scroll bars are dark in a dark menu, dropdown list and settings page.**
  `MenuStyle::FromTheme` and `DropdownStyle::FromTheme` themed everything but
  the scroll bar, and `SettingsPageKit::MakePage` turned scrolling on with a
  fresh `ContainerStyle`, putting the light look's bar back. A long context
  menu, a long dropdown list and a scrolling settings page (UltraFiler,
  UltraMail, ChatCube, UltraSnip, UltraAI) showed a light grey bar down a dark
  panel; they now take the theme's.
- **The cloud account and file picker dialogs follow the theme.** UltraCloud's
  dialog palette (`UltraCloudUiStyle.h`) was a fixed light one, so adding an
  FTP / SFTP or cloud drive in UltraFiler or UltraPress opened a white window
  in a dark app, its dropdowns black on it and the "Use as the default cloud
  account" checkbox's label white on white. The colours are the theme's roles
  now, and the buttons the theme's Primary and Standard parts.
- **The file display's notice and filter buttons are the theme's accent.**
  "Show hidden files" and the empty filter's "Scan sub folder" were a fixed
  blue; they take the theme's main-action button, as the Compress / Extract
  dialog's do.
