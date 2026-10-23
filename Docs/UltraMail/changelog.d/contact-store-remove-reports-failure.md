- **Deleting a contact reports a failure instead of half-doing it.**
  `ContactStore::Remove` ran its three deletes (emails, phones, the contact)
  without looking at their results and committed anyway, so a delete that
  failed part-way could leave the contact's phones or emails behind and still
  report success. It now goes through `RemoveAll`: a failed statement rolls
  the whole delete back and is returned.
