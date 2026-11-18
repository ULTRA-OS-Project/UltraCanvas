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
- Stored verdicts made before this change are scanned again, so mail already
  in the inbox gets the new warnings.
