// Apps/UltraPassword/core/VaultFile.h
// The encrypted file a password vault lives in — and the same file is the
// export format. "Export vault" writes another file of exactly this kind, so a
// copy carried to another computer opens in UltraPassword there with its
// password, and "Import" reads one back. There is no second format to keep in
// step with the first, and no export path that leaves the passwords readable
// unless the user explicitly picks the plain CSV (CsvExchange.h).
//
// Cryptography — the strongest construction UltraCrypt offers, end to end:
//
//  - **Key from the master password: Argon2id** (RFC 9106, the winner of the
//    Password Hashing Competition and the current OWASP first choice). It is
//    memory-hard, so a GPU or ASIC farm guessing passwords pays for the memory
//    of every guess. The default profile is libsodium's "sensitive" cost —
//    1 GiB and 4 passes — which is well above the OWASP minimum (19 MiB, 2
//    passes); a "strong" 256 MiB profile exists for machines that cannot spare
//    1 GiB for a second. The cost is stored in the file, so it can be raised
//    later without orphaning existing vaults, and it is authenticated, so it
//    cannot be lowered by editing the header.
//  - **Cipher: XChaCha20-Poly1305** with a 256-bit key and a fresh random
//    192-bit nonce on every save. Authenticated encryption: one bit changed
//    anywhere in the file and it refuses to open, rather than decrypting to
//    something subtly different.
//  - **The 60-byte header is the AEAD's associated data.** Magic, version,
//    algorithm ids, the Argon2id cost and the salt are all covered by the tag.
//  - **One sealed blob for the whole vault.** Entries cannot be dropped,
//    reordered or swapped between files one at a time.
//  - **No plaintext fallback.** Without a crypto backend, or with an empty
//    password, nothing is created or opened.
//  - **A wrong password and a modified file give the same answer**, so the
//    error is not an oracle.
//  - **Atomic writes, owner-only file mode.** A save writes a sibling
//    temporary file, flushes it and renames it over the vault.
//  - **The derived key is held in locked, wiped memory** for the session, so
//    a save does not re-run Argon2id; it is wiped on lock and on close.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef ULTRAPASSWORD_VAULTFILE_H
#define ULTRAPASSWORD_VAULTFILE_H

#include "PasswordVault.h"

#include "UltraCrypt/UltraCryptCore.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraPassword {

// The extension of a vault file and of an exported vault.
inline const char* const kVaultExtension = "upwvault";

enum class VaultError {
    None,
    BackendUnavailable,   // built without libsodium
    InvalidArgument,      // empty password, no vault open, ...
    AlreadyExists,        // Create() over an existing file
    NotFound,
    NotAVault,            // not an UltraPassword file at all
    UnsupportedVersion,   // a newer file format than this build reads
    AuthenticationFailed, // wrong password, or the file was altered
    IoError,
    OutOfMemory,          // Argon2id could not get its memory
};

struct VaultResult {
    VaultError  code = VaultError::None;
    std::string message;
    bool Ok() const { return code == VaultError::None; }
    explicit operator bool() const { return Ok(); }

    static VaultResult Success() { return {}; }
    static VaultResult Fail(VaultError code, const std::string& message) { return {code, message}; }
};

// Argon2id cost profiles offered when a vault is created.
enum class KdfProfile {
    Maximum,   // 1 GiB, 4 passes (libsodium "sensitive"). The default.
    Strong,    // 256 MiB, 4 passes, for machines short of memory.
};

UltraCryptKdfParams KdfParamsFor(KdfProfile profile);
const char* KdfProfileName(KdfProfile profile);

class VaultFile {
public:
    VaultFile() = default;
    ~VaultFile() { Close(); }
    VaultFile(const VaultFile&) = delete;
    VaultFile& operator=(const VaultFile&) = delete;

    // Creates a new vault file holding `contents`, and keeps it open. Refuses
    // to overwrite an existing file — destroying a vault must be deliberate.
    VaultResult Create(const std::string& path, const UltraCryptSecureBuffer& password,
                       const PasswordVault& contents, const UltraCryptKdfParams& cost);

    // Opens a vault file and decrypts it into `out`.
    VaultResult Open(const std::string& path, const UltraCryptSecureBuffer& password,
                     PasswordVault& out);

    // Re-seals `contents` with the session key and a fresh nonce, atomically.
    VaultResult Save(const PasswordVault& contents);

    // New salt, new key, same cost (or `cost` when given), then saves.
    VaultResult ChangePassword(const UltraCryptSecureBuffer& newPassword,
                               const PasswordVault& contents,
                               const UltraCryptKdfParams* cost = nullptr);

    // Export: writes `contents` to `destination` as a vault file that opens
    // with the *current* master password. The destination may be overwritten,
    // but never the open vault itself.
    VaultResult ExportCopy(const std::string& destination, const PasswordVault& contents) const;

    // Export under a different password (for handing a vault to someone, or
    // for a backup with its own password). Derives a fresh key at `cost`.
    static VaultResult ExportWithPassword(const std::string& destination,
                                          const UltraCryptSecureBuffer& password,
                                          const PasswordVault& contents,
                                          const UltraCryptKdfParams& cost);

    // Import: reads any vault file without keeping it open.
    static VaultResult ReadFile(const std::string& path, const UltraCryptSecureBuffer& password,
                                PasswordVault& out);

    // Wipes the key. The file stays on disk, of course.
    void Close();

    bool IsOpen() const { return isOpen_; }
    const std::string& GetPath() const { return path_; }
    uint32_t GetKdfIterations() const { return iterations_; }
    uint32_t GetKdfMemoryKiB() const { return memoryKiB_; }

    static bool Exists(const std::string& path);

private:
    std::string            path_;
    bool                   isOpen_ = false;
    UltraCryptSecureBuffer key_;
    std::vector<uint8_t>   salt_;
    uint32_t               iterations_ = 0;
    uint32_t               memoryKiB_  = 0;
};

// Wrong-password back-off: after the third failure in a row each attempt must
// wait twice as long as the last, up to a minute. Argon2id already makes every
// guess expensive; this makes guessing at the lock screen slow as well.
class UnlockThrottle {
public:
    // Seconds before another attempt is accepted (0 = now).
    uint32_t SecondsToWait(int64_t nowSeconds) const;
    void RecordFailure(int64_t nowSeconds);
    void RecordSuccess() { failures_ = 0; notBefore_ = 0; }
    int  Failures() const { return failures_; }

private:
    int     failures_  = 0;
    int64_t notBefore_ = 0;
};

} // namespace UltraPassword

#endif // ULTRAPASSWORD_VAULTFILE_H
