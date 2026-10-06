- **Files copied with xclip arrive complete.** The X11 clipboard reads files
  from `x-special/gnome-copied-files`, whose first line is the verb `copy` or
  `cut`. A program that answers every target with the same bytes, as xclip
  does, hands over a bare URI list there, and its first file was taken for
  the verb and dropped. Only `copy` and `cut` count as the verb now.
