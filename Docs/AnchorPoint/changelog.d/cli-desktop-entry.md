- **"AnchorPoint Receive" in the Linux application menu.** A new desktop
  entry for `anchorpoint-cli` (`Apps/AnchorPoint/anchorpoint-cli.desktop`)
  opens a terminal that waits for one file from a peer on port 5040 and saves
  it in the Downloads folder - the one xdg-user-dirs names, so a localised
  folder such as `~/Téléchargements` is found, else `~/Downloads`, created if
  missing. After the transfer the terminal waits for Enter, so the result or
  the error stays on screen. It shows the anchor icon and is installed with
  the CLI, whether or not the window is built; the theme icons now install
  with the CLI too, so the entry has its icon in a CLI-only install.
  The Windows installer and MSIX package give it no Start menu entry, as for
  any command-line tool.
