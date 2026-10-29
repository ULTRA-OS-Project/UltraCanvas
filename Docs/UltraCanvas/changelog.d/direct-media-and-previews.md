- **ChatCube Direct carries pictures, videos and files.** A file goes out
  as sealed envelopes like a text: a `file` body with its name, size, kind,
  caption and preview picture, then its bytes in 32 KiB parts, each an
  envelope of its own - peer to peer over the link when it is up, else
  through the relay's queue, so a file reaches someone offline and a group
  with nothing added to the relay. The other side keeps the parts in its
  store, puts the file together and reports it with `MessageUpdate`;
  `DownloadAttachment` answers from disk or once the last part is here.
  The store keeps a message's attachment with it (schema 2). The tests send
  a picture over the link, a file through the queue to someone away, a
  picture to a group and a picture over the local network with no relay,
  each read back byte for byte.
- **Preview pictures for what is sent.** `UltraIM_SetPreviewMaker` installs
  the application's way of writing a small JPEG of a picture or a video;
  providers ask through `UltraIM_MakePreview` and send it along - Telegram's
  `inputThumbnail` on videos and documents, Matrix's `thumbnail_url` /
  `thumbnail_file` on pictures and videos, ChatCube Direct's inline preview -
  so the other side sees something before the file arrives.
- **The Matrix test covers unencrypted rooms too.** `SetCreateEncrypted(false)`
  (a test hook) makes a plain room, and the Synapse test sends a picture and
  a video through one as well, so the clear and the encrypted paths both run
  in one build.
