- **EmbeddedPreviewTest checks the Xara and CorelDRAW samples again.** It
  looked for them in `media/xar` and `media/cdr`, which no longer exist - the
  samples live in `media/vector/XAR` and `media/vector/CDR` - and a missing
  folder was reported as a `[SKIP]`, so the test passed without checking
  either format's embedded preview. It reads them from where they are (all
  six carry a preview that decodes), and a missing sample folder now fails
  the test instead of skipping it.
