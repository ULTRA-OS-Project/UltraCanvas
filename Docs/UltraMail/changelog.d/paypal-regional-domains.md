- **PayPal's German receipts were marked as PayPal impersonation.** They come
  from `service@paypal.de`, and the known-sender table listed only
  `paypal.com`: a mail that says "PayPal" from a domain that is not PayPal's
  scored `brand-impersonation` and showed the phishing strip. PayPal's
  regional domains (paypal.de, .at, .ch, .co.uk, .ie, .fr, .be, .nl, .it,
  .es, .pl, .ca, .com.au) are its own now, listed one by one as the table does
  for Amazon, eBay, Google and DHL - not "paypal under any suffix", which would
  trust a squatted paypal.xyz too.
- **A proven sender's links to its own organisation under another suffix are
  its own.** A company signing its mail as `example.de` and linking to
  `example.com` (or `.net`, `.org`, a country's own, `co.uk` included) no
  longer has that link counted as a foreign domain or shown as the warning's
  "button domain". Only when the receiving server proved the From domain, and
  not for the cheap generic suffixes (`example.top`): a forged sender keeps
  every warning.
