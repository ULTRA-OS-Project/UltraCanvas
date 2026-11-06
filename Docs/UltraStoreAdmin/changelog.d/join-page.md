- **The join QR code's link opened nothing.** `publish` now writes a page
  per partner as `join/<affiliate>/index.html`, behind
  `<catalog base>/join/<affiliate>`: who invites, what a partner earns, the
  recruiter's contact when they gave one, the partner id to name when
  signing up so the recruiter is credited, and the store's contact to sign
  up with - the new setting `store.contact`, on the Settings page and
  `settings set store.contact <address>`.
- **A stale page no longer outlives its code or partner.** The uploader
  never deletes on the server, so a page once published stayed reachable
  after its code was switched off or its partner left. A code out of the
  catalog and a partner switched off now get a page that says so (*no
  longer valid*, *this invitation is closed*), which the next upload puts
  in place of the old one. The publish report, in the window and on the
  command line, counts the code pages, the join pages and these retired
  pages.
