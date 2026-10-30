- **Import: messages saved as files come into UltraMail.** A new *Import*
  button on the toolbar picks `.eml`, Apple Mail `.emlx` and Outlook `.msg`
  files (several at once) and puts them into the folder on show of the
  account on screen. They are uploaded to the account's server, as the
  Drafts and Sent copies are, so the folder's sync brings them into the list
  and every other device sees them too. An Outlook message is built again as
  a standard message: sender, recipients, subject, date, Message-ID and
  In-Reply-To, the HTML body with its plain text, the pictures it shows and
  its attachments (an attached Outlook item is not carried over). The
  imported messages arrive marked as read. A file that is not a message is
  named in one alert with the reason, and the others are still imported.
  Reads the `.msg` through the framework's new `UltraCanvasOutlookMessage`
  (`Docs/UltraCanvas/UltraCanvasOutlookMessage.md`).
- **An attachment that is a program or a script is never opened.** *Open*
  on an `.exe`, `.bat`, `.js`, `.ps1`, `.lnk`, `.msi`, `.iso`, `.sh`,
  `.desktop`, `.appimage` and the rest
  (`FileAssociations::RunsCodeWhenOpened`) no longer hands it to the system,
  which would run it - the commonest way mail carries malware. UltraMail says
  so and offers to save it instead; it is not even written to the
  attachment cache. An unnamed part is judged by the name its media type
  gives it too.
