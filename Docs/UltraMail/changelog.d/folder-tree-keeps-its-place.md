- **The folder tree no longer scrolls back to the top while you read.** With
  more than one account the tree is taller than its pane, and every folder
  rescan - which runs after each mail check, not only when you ask for it -
  rebuilt it row by row and left the view on the first row: the folder you had
  open scrolled out of sight behind your back, moments after you had picked
  it. The rebuild now holds the view (the framework's new
  `TreeRebuildScope`): the tree comes back scrolled exactly as you left it,
  with the open folder still selected and still at the same height, even when
  the rescan found new folders above it.
- **A folder opened from outside the tree is scrolled into view.** Opening an
  account's inbox from the account bar, or a folder from a message, selected
  its row without bringing it on screen, so in a tall tree the selection was
  invisible. A row already on screen stays exactly where it is.
