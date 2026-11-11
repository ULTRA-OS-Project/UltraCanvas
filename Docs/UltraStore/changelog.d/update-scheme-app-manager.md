- **The update scheme, recorded** (the README's *Decisions* and *The App
  manager*): to keep traffic down there is one update check on a machine,
  the one signed catalog compared with everything installed, and the
  applications make none of their own. A separate App manager application
  will own it, later integrated into the OS manager / settings; until then
  UltraStore's Updates page and `ultrastore update` do it. No code changed.
