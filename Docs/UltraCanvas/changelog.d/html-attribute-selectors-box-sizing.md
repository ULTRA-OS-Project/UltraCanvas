- **HTML reader: attribute selectors.** `[name]`, `[name=value]` and the
  `~=` `^=` `$=` `*=` `|=` forms, with quoted values and the ` i` flag, now
  match; before, a rule naming one was dropped. Mailchimp writes its
  narrow-screen rules that way (`table[id=templateBody]{width:100% !important}`,
  `td[class=mcnTextContent]{…}`), so a Mailchimp newsletter in a narrow pane
  stayed 600px wide and ran off the right edge. An attribute selector counts
  like a class in the cascade. Test: `HTMLReaderTest` (`TestAttributeSelectors`).
- **HTML reader: a px width or height is the content box.** As in CSS, padding
  and border now go on top of `width: 25px` unless `box-sizing: border-box`
  says otherwise (now read; tables and form controls are border-box, as in
  browsers' own style sheets). The reader counted the padding inside, so
  Mailchimp's footer icons - `<td style="width:25px; padding:0 10px">` around
  a `width:100%` picture - were drawn 5px wide. Test: `HTMLTableLayoutTest`
  ("a px width is the content box unless box-sizing: border-box").
