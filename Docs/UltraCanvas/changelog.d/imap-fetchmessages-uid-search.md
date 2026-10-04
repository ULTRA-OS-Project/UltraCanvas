- **IMAP plug-in: the bulk `FetchMessages` fetches the messages it found.** It
  listed the mailbox with `SEARCH ALL`, which answers with sequence numbers,
  then fetched each by UID (`/;UID=n`). The two agree only on a mailbox from
  which nothing has ever been deleted. Elsewhere it fetched the wrong messages,
  or none at all once the sequence numbers fell below the lowest UID. It now
  searches with `UID SEARCH ALL`, like `FetchEnvelopes`. Checked against a fake
  IMAP server whose messages 1 and 2 carry UIDs 10 and 20: before, no messages;
  after, both.
