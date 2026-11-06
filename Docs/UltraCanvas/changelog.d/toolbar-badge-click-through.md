- **UltraCanvasToolbar: a click on an item's badge reaches the item.** The
  badge `SetItemBadge` / `SetItemBadgeCount` / `SetItemBadgeDot` puts in an
  item's corner took the click and the hover itself and did nothing with
  them, so on a small bar button - UltraDesktop's mail icon with its unread
  count - a good part of the button did not respond. The badge is no
  longer interactive; a host that gives it an `onClick` of its own sets it
  interactive again.
