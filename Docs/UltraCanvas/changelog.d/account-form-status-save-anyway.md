- **UltraCloudUI: the account form's error is read whole.** Its status line
  was a fixed two lines inside the form grid, so a server's refusal stopped
  mid-sentence ("check the address and the"). It sits under the grid now, as
  wide as the dialog and as tall as its text, and the dialog leaves it four
  lines.
- **UltraCloudUI: the edit form saves without a sign-in.** Once a sign-in in
  `ShowEditAccountDialog` has failed, a *Save without signing in* button
  stores the form as it is (a password left empty still keeps the stored
  one), for a server that cannot be reached today. Not for an OAuth account,
  which is saved without a sign-in unless *Sign in again* is ticked. Its two
  texts are in the standard set, drafted in every language.
- **FilerPaneDragTest passes on every machine.** Its search for where an
  entry sits clicks across the whole display, the details view's column
  header included, and each click there re-sorted the display or turned its
  direction round. How many clicks the last column took depended on the font
  metrics, so on some machines the two folders of one date swapped places
  between two searches and the drag aimed at the wrong one (2 of 22 checks
  failed). The search now puts the sort back after any click that changed
  it.
