- **The mail on the computer is encrypted.** The mailbox index (`mail.db`),
  the address book, the outbox, the cloud accounts and every downloaded
  message are ciphertext on disk, under a random storage key kept in the
  vault (`UltraMailStorage.h`): each database is an encrypted SQLite file
  with a key of its own derived from it, each message file is sealed whole
  with XChaCha20-Poly1305. Copied off the computer, or opened with the sqlite3
  shell or UltraDBAdmin, they show nothing; with a Primary Password nothing
  on the computer opens them without it.
  - An earlier version's mailbox is converted at the first start: the
    databases before the window opens (17 ms for the demo mailbox), the
    message files on a thread of their own. Reading takes a plain file as
    it is meanwhile.
  - Attachments opened for viewing are plain copies for the viewer, so they
    are deleted at every start now (they were kept for a week).
  - EmailCleaner reads UltraMail's mailbox with the same key, taken from the
    vault with the device key; with a Primary Password set it says it cannot.
  - `test_storage` checks the storage key, the derived keys, sealing and
    opening (a changed byte and another key refused, a plain file read),
    converting plain message files and the mailbox database in place.
