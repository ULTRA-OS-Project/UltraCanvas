- **IMAP plug-in: reading a message no longer marks it read on the server.**
  The plug-in fetched each message's header (`/;UID=n;SECTION=HEADER`) and
  body (`/;UID=n`) through libcurl URLs, and libcurl sends those as
  `UID FETCH n BODY[HEADER]` / `BODY[]`, never `BODY.PEEK[...]`. RFC 3501 has
  the server set `\Seen` for that, so every sync marked all new mail read, and
  so did caching bodies ahead of time. That was in UltraMail and in every other
  mail program on the same account. The flags were also read after the header,
  so even the copy the app kept said "read". A custom `BODY.PEEK` command is no
  way round it: libcurl passes on only the reply lines that begin with `*`, and
  the message text is lost. So `FetchEnvelopes`, `FetchMessage`,
  `FetchMessageBodies` and `FetchMessages` now read a message's flags first
  and, when it was unread, send `UID STORE n -FLAGS.SILENT (\Seen)` straight
  after the fetch (`FetchKeepingUnread`). Checked against a fake IMAP server
  that keeps `\Seen` the way a real one does: before, both messages ended up
  read; after, the unread one stays unread and is reported unread. Tests:
  `test_imap_mailbox.cpp` (`imap_keep_unread_commands`).
