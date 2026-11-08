- **An account's card counted 0 new, 0 unread and 0 waiting although its
  inbox was full of unread mail.** The counters count the mail of the folder
  with the inbox role, and the inbox's mail is always stored under `INBOX` -
  but that row could lose the role:
  - after a change of the account's server (which drops the downloaded mail
    and folders), the next inbox sync re-created the `INBOX` row with no role;
  - a server that lists its inbox as `Inbox` (an IMAP server, in any case; a
    JMAP server, by the mailbox's display name) got a second, empty `Inbox`
    folder with the role - the second "Inbox" under Inbox in the folder tree.

  The server's inbox is now always stored as `INBOX`, the row the sync makes
  for it keeps the inbox role, the stray `Inbox` folder goes at the next folder
  list (its cached messages stay - on Windows and macOS they share `INBOX`'s
  directory), and UltraMail repairs an affected mail store when it starts:
  the counters are right at once, "waiting for reply" included.
