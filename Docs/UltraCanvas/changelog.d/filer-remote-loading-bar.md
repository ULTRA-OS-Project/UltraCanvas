- **Filer widget: a remote folder being fetched no longer covers the file
  area.** While `remoteListingStatus` answered, the widget drew a turning
  ring, *Loading folder* and the host's status line in the middle of the
  display. Now it slides a thin bar along the foot of the file area, just
  above the info bar (`FilerStyle::loadingBarColor`, the accent blue
  `Color(37, 99, 235)`), over whatever the folder shows, and hands the line
  to the host for its status bar: `GetListingPendingStatus()`, and
  `onListingPendingChanged(status)` when it starts, changes and ends (`""`).
  `remoteListingStatus` is now asked after every remote listing, not only an
  empty one, so a host can show the bar over entries it answered from its
  cache while it asks the server again. An empty folder's info bar says
  *Loading folder...* rather than *0 items* meanwhile. The bar's 40 ms timer
  repaints only its own 3-pixel strip.
