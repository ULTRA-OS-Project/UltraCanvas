- **A locale catalogue that conflicts in a merge is rebuilt, not hand-merged.**
  `extract_ui_strings.py` reads a catalogue left with conflict markers from
  the two sides git keeps for it: its `@bases` and other metadata, and every
  key of either side for `Standard.json`. It then writes the catalogue whole
  from the merged sources, and refuses (leaving the markers) when the two
  sides set the metadata differently. Before, it read such a file as empty
  and silently dropped `@bases`, so the application stopped inheriting its
  base set. `check_pr_overlap.py` reports a conflicting `media/locale/en/`
  catalogue as *rebuilt*, not a conflict, and says what the second branch
  to merge runs.
