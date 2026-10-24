- **Spelling marks no longer flicker below the caret while typing.**
  `UltraCanvasTextArea` and `UltraCanvasRichTextEdit` dropped every spell error
  whose bytes no longer matched after an edit - which, since an edit shifts
  everything after it, was every mark from the caret to the end of the
  document. They vanished at each keystroke and came back with the next
  result. The new `SpellCheckText::RebaseErrors(errors, before, after)` carries
  them across the edit instead: errors after it shift (byte, codepoint, line
  and column), errors it touched are dropped, and one merely adjacent survives
  unless the edit extended its word. The elements also ignore a result for an
  older job than the last one queued, whose offsets describe text that no
  longer exists.
- **Automatic spelling language detection.**
  `UltraCanvasSpellChecker::DetectLanguage(text)` checks a sample of a text's
  words against each installed language (one dictionary per base language,
  the current one first) and returns the best fit with its error rate, leaving
  the active language as it was. With `SetLanguageDetectionOptions({enabled =
  true})` the worker runs it by itself when a check flags more than 70% of at
  least 10 words, and a dictionary flagging fewer than 10% comes back as
  `SpellCheckResult::suggestedLanguage`; both thresholds are options. The
  service never switches by itself: `UltraCanvasTextArea::onSpellLanguageSuggested`
  and `UltraCanvasRichTextEdit::onSpellLanguageSuggested` hand the suggestion
  to the host. Results now also carry `checkedWordCount` and
  `misspelledCount`. Off by default.
