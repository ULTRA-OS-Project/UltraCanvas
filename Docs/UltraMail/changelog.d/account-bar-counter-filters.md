- **The account bar's counters filter the message list.** A click on
  *New today* shows the account's inbox with only its unread mail from today,
  *Unread* only its unread mail from before today, and *Waiting for reply*
  only the mail waiting for an answer - exactly the messages each number
  counts. A click anywhere else on the account's card (tile or summary card)
  shows all of its mail again. Each counter has its own tooltip, and the one
  in use is outlined in its colour.
- **A filter card under the account.** While the list is filtered, a second
  row under the account bar holds a card in the look of the account tiles,
  placed under the account's own tile (it follows the tile when it is dragged
  to another place). It names the filter ("Showing: New today") and has a
  **Show all emails** button. It also shows a filter chosen from the list's
  *Show emails ▸* menu, and goes when switching folders clears the filter.
- `UltraMailAccountBarTest` clicks every counter and checks the filter card's
  place; `UltraMailFolderTreeTest` checks that each counter's filter shows
  exactly the messages it counts; `test_messagefilter` checks the two new
  filters.
