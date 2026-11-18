- **Spam whose links are written `https:////host` is caught.** A link with
  extra slashes after `https:` (or backslashes: `https:\\host`) still opens
  the site in the browser, but the scan read it as a link with no site, so
  none of the link checks applied to it. A "Rolex from $250" mail that did
  this everywhere showed no warning at all. Those links now keep their site,
  and writing a link that way is itself a warning (`link-disguised-address`,
  under the phishing switch).
- **Fake designer goods are told.** A mail selling a luxury watch or bag
  (Rolex, Patek Philippe, Cartier, Louis Vuitton, a Birkin ...) for less than
  any genuine one costs, or calling it a "replica", "1:1 copy" or "AAA
  quality", is marked (`counterfeit-goods`), and the reading pane says
  *counterfeit goods*, with advice not to order or pay. It has its own switch
  in *Settings > Warnings > Spam/scam warnings*. Not for the brand's own
  domain; a shop that proved its own domain, in its own bulk mail, only gets
  a hint for a low price.
- **SEO offers from strangers are marked as spam.** "I noticed a small SEO
  issue on your website ... would you like the audit report?" - an offer of
  search engine services for the reader's own website - gets the *Likely
  spam* badge and a warning strip with advice not to answer
  (`seo-solicitation`), more so from a free mailbox and as a "Re:" to a
  mail never sent. A sender in the address book is not called spam for it
  (the reader's own SEO agency), and a newsletter from a domain that proved
  itself only gets a hint. It has its own switch in *Settings > Warnings >
  Spam/scam warnings*. "SEO" counts in capitals only, so a friend called
  Seo is not taken for a sales pitch.
- **The settings window speaks your language on two more counts.** The
  switches on *Spam/scam warnings* and the "Restore default" button of every
  settings page were shown in English whatever the language; they are now
  translated like the rest of the window.
- Stored verdicts made before this change are scanned again, so mail already
  in the inbox gets the new warnings.
