- **UltraIM: the owner's profile picture.** `UltraIMAccountProfile` gains
  `picturePath` (a square JPEG, PNG or WebP, "" for none), `pictureSupported`
  (filled by `FetchAccountProfile`) and `pictureChanged`: an update applies
  the picture only with the flag, so a host that never shows it cannot
  remove it by accident. The provider module ABI is 13. ChatCube Direct
  keeps a copy, at most 32 KiB, and sends it to each contact as a sealed
  `picture` body through its outbox - over the data channel, else the
  relay's queue - again when it changes and to a contact added later (each
  contact remembers the version it was sent); a contact's picture comes
  back as `avatarPath` and `senderAvatarPath`. Covered in
  `DirectProviderTests` (`TestProfilePicture`).
- **The profile picture keeps under a size.** `CropProfilePictureWithin`
  writes the framed square and, when the file comes out larger than asked,
  again as JPEG at a lower quality, then smaller, until it fits;
  `UltraCanvasProfilePicture::SetMaxOutputBytes` uses it for a chosen
  picture. The editor row's buttons are reachable
  (`GetChooseButton`, `GetRemoveButton`) for an application that styles its
  own. Covered in `ProfilePictureTest`.
