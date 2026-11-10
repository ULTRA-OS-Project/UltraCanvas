- **UltraCanvasFilerWidget: Refresh in the context menu.** The file menu
  gains *Refresh* (after *New*, with `media/icons/menu/refresh.svg`): the one
  folder entry it was opened on, or the folder on display, read again. A host
  that answers remote folders from a cache sets the new `onRefreshRequested`
  and is handed that folder to refresh itself - UltraFiler asks the server
  again and brings its tree in line; without the hook the display re-reads
  its folder (`Refresh()`).
- **UltraCloudUI: `ShowEditAccountDialog`.** A stored account in the
  add-account form, to be changed: every field filled in from the store, the
  provider shown but fixed, an FTP server's URL back in its protocol, server
  and port fields, and the password field empty - an empty one keeps the
  stored password. Saving signs in and stores only what the provider accepts,
  under the account's own id; an OAuth account is saved as it is unless
  *Sign in again* is ticked. Over a shared or a caller-kept service, like
  `ShowAddAccountDialog`; false when the account or its provider is not
  there. Its five new texts are in the standard set, drafted in every
  language.
