- **The compose window looks like a mail program's.** The formatting tools
  are icons now - bold, italic, underline, strikethrough; left, centre,
  right; bulleted and numbered lists, quote in and out; a line, a link, a
  picture - flat, in groups with thin separators, each named by its tooltip.
  They sit on a tinted strip framed together with the message body, so the
  body is a clear place to write rather than an open white area. The colour
  swatches have a text-colour mark in place of the word "Colour", and
  *Plain text | Formatted* shows a mark for each. *Send*, *Attach file…* and
  *Cloud link…* (was *Attach cloud link…*) carry icons. The signature
  editor has the same toolbar and frame.
- **Dialogs open with a header**: an icon tile, a title and the line that
  says what the dialog is for - Add an email account, the master password,
  the browser sign-in code, the wait while server settings are looked up or
  a sign-in completes (with a moving busy bar), Account settings and the
  signature editor. Their action buttons carry icons (Continue, Save,
  Unlock, Delete account, Edit signature…, Show info, Send now, Edit…,
  Add contact, Add group), and the settings window's sections (Mail,
  Reading, Privacy, Display, Warnings) have icons in its tree. The icons
  are UltraMail's own set, `media/icons/ultramail/`.
- **The signature editor's Save and Cancel are shown again.** Its button
  row was sized with a width of 0, which kept the dialog's column from
  stretching it, so the row - *HTML source*, *Cancel* and *Save* - was laid
  out zero pixels wide and never drawn.
- Demo: `ULTRAMAIL_DEMO_DIALOG=<name>` opens one dialog on its own to look
  at it (`wizard`, `password`, `password-new`, `oauth`, `wait`, `outbox`,
  `settings`, `account`).
