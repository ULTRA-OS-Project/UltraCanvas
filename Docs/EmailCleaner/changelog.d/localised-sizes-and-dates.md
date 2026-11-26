- **Sizes and dates in the person's language and conventions.** A
  message's, an attachment's and a sender's sizes are
  `UltraLocalisation::FormatFileSize`: `1,2 MB` on a German desktop,
  `512 bytes` rather than `512 B` below a kilobyte. A message's date, a
  sender's first and last mail and a contact's last message are
  `UltraLocalisation::FormatDate` (the short style, in local time):
  `14.08.2026` in German, `8/14/2026` in American English, where every
  language read `14 Aug 2026` and the day was UTC's. The engine links
  `UltraLocalisation` for them.
