- **A menu in its own window answers the keyboard wherever the key lands.**
  Where the focus follows the pointer (X11 with no window manager), a key
  pressed over a popup went to the popup's own window, which did not ask for
  keys, so X passed it on to the root: Escape left the menu open, the arrow
  keys and Enter did nothing, and the next click was spent closing the menu.
  The popup's X11 window now takes key presses (`KeyPressMask`), and the
  application hands keyboard events from a popup's window to the window the
  popup belongs to, as it already did the pointer. With a window manager
  nothing changes: a popup window never has the focus there.
  `PopupOwnWindowTest` checks Escape sent to the menu's own window.
- **UltraIM: a group's whole member count.** `UltraIMConversation::memberCount`
  says how many are in a group whose `memberIds` list only a first page; 0 when
  `memberIds` is everyone. Provider ABI 7. Telegram now lists a supergroup's
  members - the 200 most recently active (`getSupergroupMembers`), its creator
  as `ownerId` - with the total in `memberCount`, asked again when someone
  joins or leaves, and after the provider's own add and remove.
- **UltraIM Direct: renaming a group on a relay older than 1.3.0** says that
  the relay needs updating, instead of "the relay refused the request" (the
  older relay answers `405`).
