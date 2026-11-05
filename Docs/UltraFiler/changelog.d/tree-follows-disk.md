- **The folder tree drops a folder deleted in the file pane, even when the
  delete stopped at a locked file first.** Windows kept such a folder listed
  until the program holding the file let go. The tree looked at the folder
  the moment the delete reported, saw it still there, and kept its row. The
  tree now also follows what a pane's folder watch sees change on disk. A
  folder that goes later, or that another program deletes or creates in a
  folder a pane shows, is brought in line as soon as the pane re-lists it.
