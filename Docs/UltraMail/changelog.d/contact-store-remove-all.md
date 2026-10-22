- **`ContactStore::RemoveAll`: a batch of contacts deleted in one
  transaction.** `Remove` commits each contact on its own, so deleting
  thousands of them - EmailCleaner's Contacts page clearing out senders
  UltraMail collected - paid a commit per contact, which on Windows held the
  window for tens of seconds, as collecting them one by one did before
  `SaveAll`. `RemoveAll(ids)` deletes the contacts with their emails and
  phones all together, or none of them; an id that names no contact is not an
  error.
- **Deleting a contact reports a failure instead of half-doing it.**
  `ContactStore::Remove` ran its three deletes (emails, phones, the contact)
  without looking at their results and committed anyway, so a delete that
  failed part-way could leave the contact's phones or emails behind and still
  report success. It now goes through `RemoveAll`: a failed statement rolls
  the whole delete back and is returned.
