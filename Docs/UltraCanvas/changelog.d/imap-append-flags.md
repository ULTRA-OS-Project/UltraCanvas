- **The IMAP plug-in's `AppendMessage` now sets the flags it is given.** The
  upload went out without them, so a message filed with `\Seen` or `\Draft`
  (UltraMail's Drafts and Sent copies) arrived unread and not marked as a
  draft. After a successful APPEND the plug-in finds the new message by its
  Message-ID (`UID SEARCH HEADER Message-ID`, the newest match) and stores the
  flags on it; a message without a Message-ID, or a server that does not
  find it, keeps the server's defaults - the upload itself still succeeds.
  New pure helpers in `ImapParse.h`: `RawHeaderValue` (a header from the
  header block of a raw message, continuation lines unfolded) and
  `SearchByMessageIdCommand` (the quoted, escaped search). Tested in
  `test_imap_mailbox.cpp`; checked against a Dovecot 2.3 server.
- **`UltraNet_MimeBuild` takes a Message-ID passed in `extraHeaders`.**
  `in.messageId` still wins; without it a non-empty `Message-ID` among the
  extra headers (any capitalisation) is used, and only then is one made up -
  never two headers. The SMTP plug-in passes a message's headers through as
  extra headers, so a sender can now choose the ID it sends with (UltraMail
  sends a message with the ID its Drafts and Sent copies carry). Test:
  `mime_build_takes_message_id_from_extra_headers`.
