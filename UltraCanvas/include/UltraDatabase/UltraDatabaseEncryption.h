// include/UltraDatabase/UltraDatabaseEncryption.h
// SQLite files encrypted at rest: every page of the database, of its WAL and
// of its rollback journal, sealed with XChaCha20-Poly1305 (UltraCrypt) under
// a 32-byte key the application holds - typically a random key it keeps in
// its own UltraVault, so changing the vault's password never touches the
// database. Works on the system's own libsqlite3: the encryption is a VFS
// (SQLite's file layer) in front of the platform's, not a patched SQLite.
//
//   UltraDbConnectionConfig mail;
//   mail.name          = "mail";
//   mail.database      = dataDir + "/mail.db";
//   mail.encryptionKey = key;                 // std::shared_ptr<const UltraCryptSecureBuffer>
//   UltraDb_RegisterConnection(mail);         // queried as any other connection
//
// A connection with a key opens only an encrypted file (or creates one); a
// plain file is refused with a pointer to UltraDb_SetSqliteFileEncryption,
// which turns an existing file into an encrypted one (and back, and from one
// key to another) in place. Without the key an encrypted file is not a
// database to SQLite, UltraDBAdmin or the sqlite3 shell: its first bytes say
// "UltraDB:XC20P:1" and the rest is ciphertext.
//
// What it protects: the content of every page - rows, indexes, the schema -
// and that no page is changed or moved within the file without the open
// failing (each page's tag covers its number). What it does not hide: the
// file's size and how many pages it has; that WAL frames and journal records
// carry SQLite's own checksums of the page as SQLite sees it (computed above
// the encryption, so a guess of a whole page's content could be confirmed);
// and nothing stops an older copy of the file from being put back.
//
// On disk: 4096-byte pages, the last 40 bytes of each the nonce (24) and the
// tag (16) - SQLite's "reserved bytes", so the database keeps 4056 usable
// bytes a page. Temporary files are kept in memory (temp_store=MEMORY) on an
// encrypted connection, so nothing it sorts or spills reaches the disk in
// the clear; the encrypting VFS refuses to create one.
//
// Built wherever UltraCrypt is linked into UltraDatabase
// (UltraDb_SqliteEncryptionAvailable); elsewhere a key is refused with
// Unsupported rather than ignored.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDatabaseCore.h"

#include <string>

class UltraCryptSecureBuffer;

// Whether this build can encrypt SQLite files.
bool UltraDb_SqliteEncryptionAvailable();

// What a file on disk is, read from its first page without opening it.
enum class UltraDbSqliteFileKind {
    Missing,     // no such file, or an empty one (a new database)
    Plain,       // an ordinary SQLite database
    Encrypted,   // a database encrypted by this module
    Other        // something else, or unreadable
};
UltraDbSqliteFileKind UltraDb_InspectSqliteFile(const std::string& path);

// Does `key` open the encrypted file at `path`? Reads and checks its first
// page only. False for a plain file, a missing one, a wrong key.
bool UltraDb_SqliteKeyOpens(const std::string& path, const UltraCryptSecureBuffer& key);

// Rewrites the database at `path` in place: from `fromKey` (nullptr: the file
// is plain) to `toKey` (nullptr: write it plain). Encrypting a plain file,
// decrypting one and changing the key are all this. The database is read
// through SQLite - its WAL included - and written whole to `<path>.rekey`,
// checked with the new key, synced, and moved over the old file; until that
// last step the old file is untouched, and a `<path>.rekey` left by an
// interrupted run is removed. No connection may have the file open meanwhile
// (UltraDb_CloseConnection first). Missing file: nothing to do, Ok.
UltraDbResult UltraDb_SetSqliteFileEncryption(const std::string& path,
                                              const UltraCryptSecureBuffer* fromKey,
                                              const UltraCryptSecureBuffer* toKey);
