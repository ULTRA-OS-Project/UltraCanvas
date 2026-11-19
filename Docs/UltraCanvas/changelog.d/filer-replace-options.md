- **Replace when newer, bigger or different.** The file display's question for
  a name that is taken ("photo.jpg already exists in ...") has a foldable
  **Replace options** panel under the two files: Replace always (as before),
  or only when the pasted file is newer, bigger, or different (another size
  or date) - `PasteReplaceCondition`. A file that does not win is left as it
  is and listed in the summary with why ("Kept: the existing file is not
  older"). With "Apply to all remaining conflicts" the condition holds for
  the rest of the paste, a merged folder's files included. Folded, the
  panel's header still names the choice in force, and the next question of
  the session opens with it.
- **Copy skipped items again can replace.** The summary at the end of a copy
  or move that skipped files whose name was taken has an **Options for
  copying again** panel: ask about each file, keep both (the copy gets a
  number, "photo (2).jpg"), replace always, or replace when newer, bigger or
  different. The choice stands in for the host's conflict policy: under a
  standing Skip (UltraFiler's Settings > Handling > File operations) the copy
  again used to skip every file again.
- **"File exists already"** is the reason the summary gives for a file
  skipped because its name was taken (it was "Skipped: the name is already
  taken").
- **Replace when older; keep both but skip identical files.** The panel (now
  titled *Options*) adds *Replace only when the pasted file is older*
  (`PasteReplaceCondition::IfOlder`, for bringing an earlier version back
  from a backup) and a checkbox *Keep both: skip files that are identical
  (same size and date)* (`PasteConflictAction::KeepBothUnlessIdentical`): no
  duplicate of a file that is there, nothing overwritten. The summary's copy
  again offers both as well; a file skipped this way is listed as "An
  identical file exists already".
