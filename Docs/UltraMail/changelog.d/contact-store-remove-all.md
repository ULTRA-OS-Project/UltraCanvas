- **`ContactStore::RemoveAll`: a batch of contacts deleted in one
  transaction.** `Remove` commits each contact on its own, so deleting
  thousands of them - EmailCleaner's Contacts page clearing out senders
  UltraMail collected - paid a commit per contact, which on Windows held the
  window for tens of seconds, as collecting them one by one did before
  `SaveAll`. `RemoveAll(ids)` deletes the contacts with their emails and
  phones all together, or none of them; an id that names no contact is not an
  error.
