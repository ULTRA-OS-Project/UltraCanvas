- **The vault tree no longer jumps back to its first row when it is rebuilt.**
  Every change - a rename, a move, an entry added or deleted, a group opened -
  lists the tree again from the vault, and the view fell back to the top each
  time, however far down the user had scrolled. The rebuild now holds the view
  (the framework's `TreeRebuildScope`): the scroll position comes back as it
  was and the selected row keeps its place on screen. The rows that were open
  are restored as before.
