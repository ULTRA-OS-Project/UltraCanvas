- **A payment reminder whose "invoice" is a web page is marked as a scam.**
  A "Mahnung" with the invoice attached as `Rg-2608144_(563804).html`, sent
  in a plumbing firm's name from a music site's address, was not flagged:
  no rule looked at an `.html` attachment or at a company name over a
  foreign domain. Three new findings:
  - `attachment-web-page` - a web page attached (`.html`, `.htm`, `.xhtml`,
    `.mht`) opens in the browser, outside the mail program: Suspicious, and
    40 points when the mail or the file name speaks of an invoice, a reminder
    or a payment, since real invoices come as PDFs.
  - `attachment-active-page` - the attached page (or an SVG picture) is read
    as the browser would run it: one that loads a script from a website, has
    a form, sends the browser on or decodes a hidden page is a Scam on its
    own. The reminder's page loaded a script from a hacked site. It still
    counts for a sender you trust (their mailbox may be the one hijacked).
  - `sender-company-mismatch` - the display name names a company whose own
    domain the mail itself gives ("questions to fibu@bug-rohrreinigung.de")
    while it came from an unrelated domain. Not for a free mailbox, "… via
    …", or a sender domain carrying the company's name; a domain DKIM or
    DMARC proves (an invoicing service) gets only a hint.
- The reading pane names such a mail *phishing* and advises not to open the
  attached page. The rules revision moves to 2026-10-27, so stored mail is
  scanned again.
