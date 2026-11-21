- **The settings tables follow a change of look.** The tables of Last used
  folder, Notification registrations and Trusted certificates took their
  background, header and rule colours once, when the page was built, while
  the labels in them follow a theme change - so after a switch of look a
  dark table stayed in a light window, its text turned dark on dark. The
  tables, their headers, rows and scrollbars now take the theme's colours
  again after every change (`FollowKitLook`).
- **The tables and the certificate page are translated.** The column
  headers (Application, Click starts, Opens in, Last used folder, Device,
  Address, Key), the "Click starts" cells, the certificate page's notes and
  status line, the Last used folder notes and "Saved to" went into the
  window untranslated; they go through `Tr` now, translated in all thirty
  languages (machine drafts). The activator's CLSID moved from the cell to
  its tooltip. pt-BR also gets the eight older strings it lacked.
