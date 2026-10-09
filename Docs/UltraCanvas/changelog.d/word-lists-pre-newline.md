- **Word's lists are lists in the editor and in mail replies.** Word writes
  a list as paragraphs - `<p style="mso-list:l0 level1 lfo1">` with the
  label it shows typed out in front, on the clipboard and in every mail
  Outlook sends. `ImportHTMLToRichDocument` read them as plain paragraphs: a
  reply to an Outlook mail quoted "1.  First" as text, and a paste from Word
  lost the numbers altogether. Such a paragraph is now a list item at its
  level, its label the marker: numbers, letters and Roman numerals give a
  numbered item in that format, starting where Word's did (a list from 4, or
  one Word carries on past a paragraph); Word's bullets become the model's
  (a circle for its `o`, a square for its `§`). A numbered heading stays a
  heading. `skipWordListLabels` now only concerns such a heading's number.
- **The newline right after `<pre>` is the only one dropped.** HTML's tree
  builder drops a newline that directly follows a `<pre>`, `<listing>` or
  `<textarea>` start tag; `HTML::Parser` kept it. The element builder, which
  shows a `<pre>`'s text as written, drew an empty first line for a `<pre>`
  whose content starts on the next source line (the mail view, the e-book
  reader), and the rich-document importer, making up for it, dropped every
  blank line at the start of a `<pre>`. The parser drops that one newline
  now, and the importer keeps the rest.
- **macOS CI keeps the libraries' sources.** A run that has to rebuild a
  library from source (its package missing from the vcpkg binary cache)
  downloaded it from its home site, so `download.gnome.org` being down on
  2026-10-08 turned the macOS leg red. CI now keeps every source in vcpkg's
  asset cache, fetched once per change of `MacOS/deps` or the vcpkg commit
  (`scripts/macos-deps.sh` gains `UC_VCPKG_ONLY_DOWNLOADS=1` for that), and
  builds from that copy.
