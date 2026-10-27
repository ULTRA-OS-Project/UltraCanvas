- **UltraDatabase encrypts SQLite files at rest.** A connection with an
  `encryptionKey` (32 bytes the application holds) reads and writes a file
  that is ciphertext on disk: every page of the database, of its WAL and of
  its rollback journal is sealed with XChaCha20-Poly1305 by a VFS in front of
  the platform's, so it works on the system's own libsqlite3 everywhere and
  no query changes (`UltraDatabaseEncryption.h`).
  - A wrong key, a plain file opened with a key and a changed byte are each
    refused as what they are; without the key the file is not a database to
    SQLite, the sqlite3 shell or UltraDBAdmin.
  - `UltraDb_SetSqliteFileEncryption` encrypts a plain file, decrypts one or
    changes its key in place, its WAL included, and swaps the file only once
    the new one has checked; `UltraDb_InspectSqliteFile` and
    `UltraDb_SqliteKeyOpens` tell what a file is without opening it.
  - Temporary data of an encrypted connection stays in memory; a WAL left by
    a crash and a hot journal are recovered through the encryption.
  - `UltraDatabaseTests` gains nine encryption tests (103 in all), among them
    recovery from a WAL and a journal copied mid-write.
