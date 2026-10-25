- **Incoming mail never adds a no-reply address to the contacts.** A known
  service was filed under Services for whatever address it wrote from, and
  some services send from a new `no-reply-<code>@...` address in every
  message - Anthropic's mail had put 77 "Anthropic" contacts in Services,
  one per message. Nobody writes to a no-reply address (it was already never
  offered as a recipient suggestion), so the collector now skips it, known
  service or not; the service still reads as a business sender without a
  contact. Writing to such an address yourself still adds it.
- **Services > right-click > *Remove no-reply contacts…*** removes what the
  old rule left behind: the no-reply contacts UltraMail filed by itself -
  one address, no phone, no group, and the "collected automatically" note -
  after a confirmation that lists the first few. A no-reply contact you
  filed yourself is kept.
- `test_contacts` checks that a no-reply sender is not collected and what
  `ContactCollector::CollectedNoReply` finds.
