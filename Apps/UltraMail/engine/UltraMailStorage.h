// Apps/UltraMail/engine/UltraMailStorage.h
// The mail UltraMail keeps on the computer, encrypted at rest: the mailbox
// index (mail.db), the address book (contacts.db), the outbox (outbox.db),
// the cloud accounts (cloud.db) and every downloaded message (mail/.../*.eml).
//
// One random 32-byte storage key, kept in UltraMail's vault
// ("mail.ultramail.storage-key"), and keys derived from it with HKDF-SHA256:
// one per database file - so a page of one file never opens in another - and
// one for the message files. The vault's own passphrase guards the storage
// key: the device key beside it by default (the mail files are then unreadable
// on their own - a backup, a copied folder, a disk taken out - but not to
// this computer account), or the Primary Password, which nobody but the user
// has. Changing the Primary Password never touches the mail: the storage key
// stays the same, only the vault around it is re-encrypted.
//
// The databases are SQLite files encrypted page by page by UltraDatabase
// (UltraDatabaseEncryption.h); a plain one from an earlier version is
// converted in place the first time (EncryptDatabaseFile). A message file is
// sealed whole with XChaCha20-Poly1305:
//
//   "UltraMail:eml:1\0" (16 bytes, the associated data) | nonce (24) | ciphertext | tag (16)
//
// Reading takes either form - a plain file is one not converted yet - and
// EncryptStoredBodies converts the plain ones in the background.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailCredentialVault.h"

#include <UltraDatabase/UltraDatabase.h>

#include <functional>
#include <memory>
#include <string>

class UltraCryptSecureBuffer;

namespace UltraMail {

using StorageKey = std::shared_ptr<const UltraCryptSecureBuffer>;

// The vault account (Store / Retrieve) the storage key is kept under.
inline constexpr const char* kStorageKeyAccount = "storage-key";

// The storage key from `vault`, which must be unlocked. With `create` a new
// one is made and stored the first time; without it (another program reading
// UltraMail's files) a vault without one answers nullptr and no error.
// nullptr with `error` when the vault cannot be read or written, or this
// build has no crypto backend.
StorageKey LoadStorageKey(CredentialVault& vault, bool create, std::string& error);

// The storage key of the UltraMail mailbox in `mailDataDir`, for another
// program that reads its files (EmailCleaner): its vault opened with the
// device key and only read - never created, never written. nullptr with
// `problem` when it cannot be opened (a Primary Password is set, no crypto
// backend); nullptr and no problem when there is no vault or no key in it
// (an earlier version's mailbox, whose files are plain).
StorageKey ReadMailboxStorageKey(const std::string& mailDataDir, std::string& problem);

// The key of one database file ("mail.db", "contacts.db", ...) and of the
// message files, derived from the storage key. nullptr for a nullptr key.
StorageKey DatabaseKey(const StorageKey& storageKey, const std::string& fileName);
StorageKey BodyKey(const StorageKey& storageKey);

// The database at `path` made an encrypted one under `key` when it is a
// plain one (an earlier version's); nothing for a missing or an encrypted
// file. No connection may be open on it.
UltraDbResult EncryptDatabaseFile(const std::string& path, const StorageKey& key);

// ---- message files ---------------------------------------------------------

// The key message files are written with, for the whole process; nullptr
// (the default) writes them plain. Reading needs it for a sealed file.
void SetBodyKey(StorageKey key);
StorageKey GetBodyKey();

// Does `stored` (a file's bytes) start like a sealed message file?
bool IsSealedBody(const std::string& stored);

// `raw` sealed with `key`, in the format above; false without a key.
bool SealBody(const StorageKey& key, const std::string& raw, std::string& out);
// `stored` as the message it holds: opened with `key` when sealed, as it is
// when plain. False when sealed and the key does not open it (or is none).
bool OpenBody(const StorageKey& key, const std::string& stored, std::string& out);

// A message file read (and opened with the body key); false, with `error`,
// when it cannot be read or does not open.
bool ReadBodyFile(const std::string& path, std::string& out, std::string* error = nullptr);
// A message file written - sealed with the body key when there is one - to
// `<path>.part` and moved into place, so a reader never sees half of one.
bool WriteBodyFile(const std::string& path, const std::string& raw, std::string* error = nullptr);

// Every plain *.eml under `mailDir` sealed in place with the body key,
// until `stop()` says so. The number sealed.
size_t EncryptStoredBodies(const std::string& mailDir, const std::function<bool()>& stop = {});

} // namespace UltraMail
