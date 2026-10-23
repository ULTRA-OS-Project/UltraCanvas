- **A redesigned main window.** A white band at the top carries the logo,
  the app's name and how many accounts it holds, with Settings and Lock at
  its right; under it, *Scan QR code* (the main action, in the accent
  colour) and *Enter key*, and to the right, quieter, *Password…* (change
  the master password), *Back up…* and *Restore…* - each with an icon.
  What the last action did is reported in a status line at the foot of the
  window instead of between the buttons and the list.
- **Cards are easier to read at a glance.** Each is a rounded panel with a
  coloured monogram for the service, the code
  in large digits, and a ring that empties as the code's time step runs out,
  with the seconds left in its centre and the same green / amber / red as
  before (`UltraCanvasGaugeDiagramElement`, `CircularRing`). *Edit*, *Show
  key* and *Remove* are icon buttons with tooltips, so twenty accounts are
  no longer a wall of buttons; *Remove* turns red under the pointer. The
  empty list says how to add the first account.
- **A well-known service's monogram is in its own brand colour** - GitHub's
  near-black, Dropbox's blue, AWS's orange, and about eighty more
  (`BrandColors.h`). Only the colour and the first letter, never the logo,
  which is a trademark. The letter turns dark on a light brand colour
  (Snapchat's yellow, AWS's orange) so it stays readable. Any other service
  gets a colour picked from its name, the same one at every launch.
- **The dialogs match.** Every dialog's main button is the accent colour and
  the other is plain; text fields and drop-downs have rounded corners and
  the accent border while focused. *Add account* had kept the framework's
  default look, and is styled like the rest now.
- The icons are new, in `media/icons/authenticator/`; the colours, sizes and
  button styles stay in one place, `Theme.h`.
