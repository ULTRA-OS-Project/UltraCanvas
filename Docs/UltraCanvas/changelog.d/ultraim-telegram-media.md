- **UltraIM: attachments fetched and sent; the Telegram provider does
  pictures, videos, files and profile photos.** `UltraIMAttachment` gained
  `thumbnailPath` (a video's or a file's preview picture); the provider
  interface `SendVideo` and `DownloadAttachment`, and the API
  `UltraIM_SendVideo` and `UltraIM_DownloadAttachment` (a video, a file or a
  voice note fetched to disk when asked for, answered at once when it is
  there; pictures and preview pictures a provider fetches by itself and
  reports with `MessageUpdate`). The Telegram provider downloads profile and
  chat photos (`avatarPath` on conversations, contacts and a message's
  sender), photos and the preview pictures of videos, animations, video
  notes, documents and stickers through TDLib's `downloadFile` /
  `updateFile`, keeps the files behind each message so `DownloadAttachment`
  can fetch the rest, and sends a local file as a photo (`inputMessagePhoto`),
  a video (`inputMessageVideo`) or a document with a caption
  (`SendImage`, `SendVideo`, `SendFile`); its capabilities list
  `ImageMessages`, `FileTransfer` and `Stickers`. `FakeTdJson` holds files
  now (profile photos, media sent, `downloadFile`), and the provider tests
  cover a contact's picture, a photo, a video and a file each way; the
  request test hands the media requests to the real TDLib.
