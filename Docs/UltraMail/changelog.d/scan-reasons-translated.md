- **The scan's reasons are in the reader's language.** Every reason the
  scam/phishing scan gives - the warning strip's list, the sender badge's
  tooltip, the "Do you really want to open the link?" question - and the
  sender's verdict line ("New sender", "In your address book", ...) go through
  `Tr` / `TrF`, so a UltraMail catalogue translates them; a reason that lists
  several signs joins them with the language's own separators. The engine
  links UltraLocalisation (its home is the shared core, so engine and
  application share one loaded set). A message that warns has its stored
  reasons written again in the current language when it is shown, from a
  fresh scan that came to the same findings, so a change of language does not
  leave old mail in the old one.
