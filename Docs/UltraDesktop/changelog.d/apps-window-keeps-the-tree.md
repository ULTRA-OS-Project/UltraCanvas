- **The Apps window's category tree keeps its place while you type in the
  filter box.** Every keystroke - and every new list of applications, and
  every application moved to another category - built the window's pane again
  from nothing, tree element and all, so the category you had chosen, the
  categories you had opened and the place you had scrolled to went with the
  discarded element each time. The tree and the list beside it are now kept
  and only the tree's rows are replaced: the view stays where it was (the
  framework's `TreeRebuildScope`), the categories you opened open again, and
  the category you were looking in stays selected with its applications still
  listed. Switching to the tiles and back, or a filter that matches nothing,
  builds the pane afresh as before.
