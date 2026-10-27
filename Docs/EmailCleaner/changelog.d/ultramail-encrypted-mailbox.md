- **Reads UltraMail's encrypted mailbox.** UltraMail now keeps its databases
  and message files encrypted at rest; EmailCleaner takes the storage key from
  UltraMail's vault with the device key (never writing to it) and opens the
  account list, the address book and the cached messages with it. A mailbox
  whose vault has a Primary Password stays closed, and EmailCleaner says so
  instead of asking UltraMail to store a device key.
