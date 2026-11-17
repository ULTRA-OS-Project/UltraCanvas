- **Mattermost and Zulip.** Two team chats join ChatCube, with buttons in a
  third row: your server, your user name or e-mail and your password (and,
  for a Mattermost account with two-factor sign-in, the code from your
  authenticator app). Mattermost brings your direct and group messages and
  your teams' channels, with thread replies; Zulip your direct and group
  messages and each topic of your streams as a chat. Texts, files of any
  kind with the progress bar, edits, deletes, read marks, search; the next
  start signs in by itself. The status line shows the account's name.
- **Which network?** An address more than one signed-in network takes
  (`@user`: Telegram and Mattermost; an e-mail: Zulip and Mattermost) asks
  which one when added; when none of them is signed in, the status line
  names them.
- **Signal.** A Signal button shows a QR code to scan with the Signal app on
  the phone: ChatCube becomes a linked device (through signal-cli, which is
  installed separately). Chats and groups, replies, edits, deletes, ticks,
  typing, pictures and files; numbers and Signal usernames in *Add*.
- **XMPP.** Any XMPP (Jabber) server, over TLS (port 5222 or 5223, found from
  your address) or WebSocket: your address and
  password, your contacts and rooms, the server's history, ticks, typing,
  edits, deletes, replies and file uploads. The password is not kept, only
  keys derived from it.
- **IRC.** Server, nickname and (for a registered one) password; `#channel`
  joins, a nickname opens a private chat, the network's own chat shows its
  notices and takes commands. Over TLS unless the server starts with
  `irc://`. A nickname in use is changed and the status line says to what.
