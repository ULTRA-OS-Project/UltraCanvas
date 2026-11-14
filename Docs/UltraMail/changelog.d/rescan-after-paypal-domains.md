- **Mail scanned before the PayPal fix kept its phishing warning.** A verdict
  is stored when a mail arrives and made again only when it is older than the
  rules (`kThreatRulesRevision`), which 0.10.94 did not move: a PayPal mail
  from `service@paypal.de` received before it still said "This is likely a
  phishing email". The revision is 2026-11-14 now, so those verdicts are made
  again - on opening the mail, and by the sync a batch at a time.
