- **IMAP: headers and bodies are fetched in batches, fifteen times faster on a
  slow link.** The IMAP plug-in fetched mail one message at a time: its flags,
  then its header or body by URL (which libcurl fetches with BODY[], marking it
  read), then the read mark taken off again - four to six round trips a
  message. Through a virus scanner that reads the mail on Windows that came to
  about a second a message: a mailbox of 2228 took over half an hour to load
  the first time. The plug-in now opens a session of its own on the connection
  libcurl signs in (`CURLOPT_CONNECT_ONLY`: TLS, STARTTLS, password or XOAUTH2
  as before) and asks for fifty headers or ten bodies in one `UID FETCH`, with
  `BODY.PEEK` - nothing is marked read, and nothing has to be put back.
  Measured against Dovecot at 150 ms a round trip: 160 messages with their
  bodies 107.8 s -> 6.9 s (0.67 -> 0.04 s a message), unread mail still
  unread. `FetchEnvelopes`, `FetchEnvelopesByUid` and `FetchMessageBodies`
  use it; the per-message way stays as the fallback when libcurl will not open
  such a session, and for the rest of a batch the server refuses. The
  responses are read whole, literals included (`ImapResponseReader`,
  `ParseFetchResponse`, `UidSetString` in `ImapParse.h`) - libcurl hands on
  only the lines of a command's response that begin with `*`, which is why a
  message's text could not be fetched in one command before. Tests:
  `test_imap_mailbox.cpp`.
