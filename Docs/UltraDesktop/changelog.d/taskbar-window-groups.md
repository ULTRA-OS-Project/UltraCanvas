- **An application's windows share one taskbar button, with their
  pictures on hover.** Three Explorer windows were three buttons; now they
  are one, with a count. Resting the pointer on it opens a strip of the
  windows - icon, title and a picture of each, the active one framed, a
  minimized one shown by its icon - and a click on a picture brings that
  window forward; its × closes it. The strip opens beside the bar on the
  left, above it at the bottom and below it at the top, follows the pointer
  along the bar and does not take the keyboard focus. The button's
  right-click menu lists the windows with *Minimize all* and *Close all
  windows*. A single window behaves as before.
- **Windows: Calculator, Settings, WhatsApp and other Store applications
  show their own icon** on the taskbar instead of a letter ("A", "W"): the
  icon now comes from the application identity Windows gives the window,
  through the shell's Applications folder.
- **On Wayland the window pictures come from the compositor** where it
  offers window capture (`ext-image-copy-capture-v1`: sway 1.11, labwc,
  river, niri and others), so a covered window shows its own content.
