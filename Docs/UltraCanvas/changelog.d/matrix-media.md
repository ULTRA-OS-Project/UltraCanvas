- **The Matrix provider fetches and sends attachments.** Pictures, and the
  preview pictures of videos and files, are downloaded as the message
  arrives (the authenticated media endpoint, else the older one), videos and
  files when `UltraIM_DownloadAttachment` asks; in an encrypted room the
  `m.file` format is read (AES-256-CTR, the hash checked first). `SendImage`,
  `SendVideo` and `SendFile` upload a local file - encrypted with a fresh key
  in an encrypted room - and send the message with its `filename`, the
  caption as the body and the type and size in `info`. `MatrixHttp` gained
  `Download` and `Upload`. The provider's capabilities list `ImageMessages`
  and `FileTransfer`, so ChatCube's paperclip works in Matrix rooms too. The
  Synapse test sends a picture, a video and a file each way and reads them
  back byte for byte.
