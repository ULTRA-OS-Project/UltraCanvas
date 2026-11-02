- **A menu layout the user arranged gains a newer program's commands.**
  `SaveMenuLayoutSet(set, path, defaults)` records the commands the
  program's menus held (`MenuLayoutSet::knownCommands`, a `[KNOWN]` section
  an older reader passes over), and `LoadMenuLayoutSet(out, path, defaults,
  registry)` / `MergeMenuLayoutSet` put each command added since at its place
  in the defaults - after the entry it follows, in the submenu of the same
  label, a new submenu or menu whole - while a command the user removed stays
  removed and one the user has elsewhere is not added twice. A saved layout
  used to load as it was, so a new command never appeared for a user who had
  customised the menus.
