- **A hidden folder in the tree is drawn lighter.** When Extras > Open
  location, a typed location or a recent one leads into a folder the tree
  leaves out - `~/.config`, `AppData`, one outside the curated Home list -
  the tree adds its row so it can select it, in name order among its
  siblings, and now draws its name in the theme's muted text colour, so it
  reads as a folder that is normally hidden. Uses the framework's new
  `TreeNodeData::dimmed`.
