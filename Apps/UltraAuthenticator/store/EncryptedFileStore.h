// Apps/UltraAuthenticator/store/EncryptedFileStore.h
// A single-file encrypted secret store: XChaCha20-Poly1305 over a key derived
// from the user's master password with Argon2id.
//
// This is the authenticator's answer to the hardest problem in the threat
// model (UltraAuthenticator-Investigation.md §3.1): TOTP seeds are equivalent
// to the second factor itself, and anyone who reads them once can generate
// valid codes forever. Google Authenticator's own history is the cautionary
// case — a long-plaintext Android database, and a 2023 cloud sync that shipped
// without end-to-end encryption.
//
// Properties, and why:
//
//  - **No plaintext path, ever.** There is no "encryption unavailable" fallback
//    that writes readable seeds. Without a crypto backend, or without a
//    password, the store refuses to open. A silent plaintext fallback is the
//    classic hole and is precisely the bug UltraCrypt was created to end.
//  - **One AEAD blob for the whole file**, so integrity covers every entry
//    together: an attacker cannot splice, reorder or drop a single account.
//    The 60-byte header — including the declared Argon2id cost — is
//    authenticated as associated data, so downgrading the cost is detected
//    rather than obeyed.
//  - **Cost parameters travel with the file.** Raising the recommended
//    Argon2id cost later must not orphan existing vaults.
//  - **The key is derived once per session, not per write.** Argon2id at 64 MiB
//    takes ~100 ms; deriving it on every Put would make the app unusable.
//    Re-using the key across writes is safe here specifically because
//    XChaCha20-Poly1305's 192-bit nonce makes a fresh random nonce per write
//    collision-free in practice — the property that motivated choosing it.
//  - **Writes are atomic**: a temporary file is written, flushed and renamed
//    over the vault, so an interrupted save cannot truncate it.
//  - **The file is created 0600.**
//
// Since container version 2 (UltraAuthenticator 0.3) the entries are sealed
// with a random **data key**, and the file carries that key twice at most:
// once sealed with the key derived from the master password, and - when the
// person turned recovery on - once more sealed with a **recovery key**. The
// recovery key is never stored whole. It is derived from two random halves:
// the *local share*, kept in this file's header, and the *server share*,
// which the person keeps as a printed recovery code and/or leaves with their
// UltraAccount (the escrow, released only after a code sent to the account's
// verified email or phone). Neither half opens anything alone: the account
// service never sees the file, and the file never holds the server share. A
// thief needs the file *and* the person's mailbox (or phone, or paper).
// Version 1 files still open; the next write upgrades them.
//
// What this does NOT defend against, stated plainly: a live attacker running
// as the same user. They can read the process's memory or wait for the vault
// to be unlocked. Defeating that needs the per-app isolation UltraVault's
// native backend anticipates, not application-level encryption.
//
// Version: 0.2.0 - container version 2: the data key, the password slot, the recovery slot
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef ENCRYPTEDFILESTORE_H
#define ENCRYPTEDFILESTORE_H

#include "ISecretStore.h"

#include <map>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace Authenticator {

// Bounds on untrusted container fields, so a malformed or hostile vault file
// cannot make the reader allocate wildly before the tag check fails.
constexpr size_t kMaxStoreEntries    = 4096;
constexpr size_t kMaxStoreKeyLength  = 512;
constexpr size_t kMaxStoreValueBytes = 64 * 1024;
constexpr size_t kMaxStoreFileBytes  = 16 * 1024 * 1024;

// The server share of a recovery key, and the bounds on what the header names
// it by.
constexpr size_t kRecoveryShareBytes       = 32;
constexpr size_t kMaxRecoveryEscrowIdBytes = 64;
constexpr size_t kMaxRecoveryServiceBytes  = 512;

// What the header says about recovery. Not secret - it is readable without
// the password, because the person who forgot the password needs it: which
// account service holds the server share and under which id. Empty
// escrowId with `enabled` means a recovery code only, kept by the person.
struct VaultRecoveryInfo {
    bool        enabled = false;
    std::string escrowId;     // the UltraAccount recovery key's id, "e-..."
    std::string serviceUrl;   // the account service that holds it
};
class EncryptedFileStore final : public ISecretStore {
public:
    EncryptedFileStore() = default;
    ~EncryptedFileStore() override;

    EncryptedFileStore(const EncryptedFileStore&) = delete;
    EncryptedFileStore& operator=(const EncryptedFileStore&) = delete;

    // Creates a new, empty vault. Fails with AlreadyExists rather than
    // overwriting — destroying a vault of second factors must be deliberate.
    // An empty password is refused.
    StoreResult Create(const std::string& path,
                       const UltraCryptSecureBuffer& password);

    // Opens an existing vault. A wrong password and a modified file both
    // report AuthenticationFailed with the same message: distinguishing them
    // would tell an attacker which of the two they achieved.
    StoreResult Open(const std::string& path,
                     const UltraCryptSecureBuffer& password);

    // Wipes the derived key and all decrypted entries from memory.
    void Close();

    bool IsOpen() const override { return isOpen_; }

    StoreResult Put(const std::string& key,
                    const UltraCryptSecureBuffer& value) override;
    StoreResult Get(const std::string& key,
                    UltraCryptSecureBuffer& outValue) const override;
    StoreResult Delete(const std::string& key) override;

    // Atomic because the whole vault is one blob: both halves land in the same
    // SaveLocked(), so there is no state on disk where the secret exists under
    // both keys, and a failed write rolls memory back to match.
    StoreResult Replace(const std::string& oldKey, const std::string& newKey,
                        const UltraCryptSecureBuffer& newValue) override;
    StoreResult List(std::vector<std::string>& outKeys) const override;
    size_t Count() const override { return entries_.size(); }

    // Re-derives the key from a fresh salt and rewrites the vault. The old key
    // is wiped; a stolen copy of the previous file remains readable with the
    // old password, as it must be — this rotates the key, it cannot reach
    // backups already taken.
    StoreResult ChangePassword(const UltraCryptSecureBuffer& newPassword);

    // --- Recovery (container version 2) -----------------------------------
    // Turns recovery on for the open vault, or replaces the recovery slot:
    // a fresh local share is drawn, the recovery key derived from it and
    // `serverShare` (kRecoveryShareBytes), the data key sealed with it, and
    // the file rewritten. The caller keeps `serverShare` - escrows it with
    // the account service and/or shows it once as a recovery code - and
    // wipes it; this class never stores it.
    StoreResult EnableRecovery(const UltraCryptSecureBuffer& serverShare,
                               const std::string& escrowId,
                               const std::string& serviceUrl);
    StoreResult DisableRecovery();
    VaultRecoveryInfo GetRecoveryInfo() const;

    // The header's recovery fields, read without a password (see
    // VaultRecoveryInfo). A version 1 file reports recovery off.
    static StoreResult ReadRecoveryInfo(const std::string& path,
                                        VaultRecoveryInfo& outInfo);

    // Opens the vault with the server share instead of the password, then
    // sets `newPassword` and removes the recovery slot - the share has left
    // its safe place, so it must not open the vault again; the person turns
    // recovery on afresh, with a new share. A wrong share and a modified file
    // both report AuthenticationFailed.
    StoreResult OpenWithRecovery(const std::string& path,
                                 const UltraCryptSecureBuffer& serverShare,
                                 const UltraCryptSecureBuffer& newPassword);

    // 1 for a vault still in the original layout (until its next write), 2
    // after.
    uint8_t GetContainerVersion() const { return containerVersion_; }

    // The Argon2id cost this vault was opened or created with.
    uint32_t GetKdfIterations() const { return kdfIterations_; }
    uint32_t GetKdfMemoryKiB() const { return kdfMemoryKiB_; }

    static bool Exists(const std::string& path);

private:
    StoreResult SaveLocked() const;
    StoreResult DerivePasswordKey(const UltraCryptSecureBuffer& password,
                                  UltraCryptKdfParams& kdf,
                                  UltraCryptSecureBuffer& outKey) const;
    StoreResult ParseFile(const std::string& path, std::vector<uint8_t>& raw,
                          size_t& outBodyOffset);
    StoreResult SerializeEntries(UltraCryptSecureBuffer& outPlain) const;
    StoreResult DeserializeEntries(const UltraCryptSecureBuffer& plain);

    bool        isOpen_ = false;
    std::string path_;

    // Held for the session so writes do not re-run Argon2id: the key derived
    // from the password, which seals the data key into the password slot,
    // and the data key, which seals the entries. In a version 1 file the
    // password key sealed the entries itself; opening one draws a data key,
    // which the next write puts on disk.
    UltraCryptSecureBuffer passwordKey_;
    UltraCryptSecureBuffer dataKey_;
    mutable uint8_t        containerVersion_ = 2;   // 2 once written

    // The recovery slot as read or last written: the local share, the slot's
    // nonce and the data key sealed with the recovery key. None of it opens
    // anything without the server share.
    VaultRecoveryInfo    recovery_;
    std::vector<uint8_t> recoveryLocalShare_;
    std::vector<uint8_t> recoveryNonce_;
    std::vector<uint8_t> recoverySealedKey_;
    std::vector<uint8_t>   salt_;
    uint32_t               kdfIterations_ = 0;
    uint32_t               kdfMemoryKiB_  = 0;

    // Decrypted contents. Keys are not secret; values are.
    std::map<std::string, UltraCryptSecureBuffer> entries_;
};

} // namespace Authenticator
} // namespace UltraCanvas

#endif // ENCRYPTEDFILESTORE_H
