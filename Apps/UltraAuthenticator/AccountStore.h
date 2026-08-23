// Apps/UltraAuthenticator/AccountStore.h
// The account layer: what the UI talks to, and the only thing that ever
// touches a seed.
//
// An account is persisted as exactly one vault entry whose value is its
// `otpauth://` URI. That choice is deliberate:
//
//  - The URI is already the interchange format every authenticator agrees on,
//    and this app already has a tested parser and builder for it
//    (otp/OtpAuthUri.h). Inventing a second on-disk shape would mean a second
//    thing to validate and keep in step.
//  - One value per account means adding, removing and rewriting an account is
//    a single Put/Delete against the vault, with no index to corrupt.
//
// The security point of this class: **the UI never sees a secret** — with one
// deliberate exception, named below. Callers get accounts (issuer, label,
// digits, period) and generated codes. The seed is read out of the vault,
// used, and destroyed inside a single call, so it exists in memory for
// microseconds rather than for as long as a window is open. Nothing here puts
// a seed in a std::string.
//
// The exception is `Reveal`. Moving to a new phone requires reading the seed
// back out; an authenticator that can only ever swallow secrets strands its
// users, and they work around it by keeping the original QR code somewhere far
// less safe. So the operation exists, and is fenced instead of forbidden:
//
//  - It demands the master password again, even though the vault is already
//    open. Unlocking happened once at launch; the person now standing at the
//    screen may not be the person who unlocked it.
//  - It is the only method that hands back an UltraCryptSecureBuffer, so the
//    grep for "who can see a seed" stays a one-line answer.
//  - Nothing else in the app calls it. The reveal dialog is its sole caller.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef ACCOUNTSTORE_H
#define ACCOUNTSTORE_H

#include "otp/UltraOtp.h"
#include "store/EncryptedFileStore.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace Authenticator {

// What the UI is allowed to know about an account. Note the absence of a
// secret field — that is the whole point.
struct Account {
    std::string    key;        // vault key; stable identity for this account
    Otp::Parameters params;    // issuer, accountName, digits, period, type, …
};

// Longest an account key may be. Keys are derived from issuer + account name,
// both of which come from a scanned QR code, so they are untrusted input.
constexpr size_t kMaxAccountKeyLength = 256;

class AccountStore {
public:
    AccountStore() = default;
    ~AccountStore() = default;

    AccountStore(const AccountStore&) = delete;
    AccountStore& operator=(const AccountStore&) = delete;

    // Creates a new vault. Fails if one already exists at that path.
    StoreResult Create(const std::string& path,
                       const UltraCryptSecureBuffer& password);

    // Opens an existing vault. A wrong password and a tampered file are
    // reported identically, by design.
    StoreResult Open(const std::string& path,
                     const UltraCryptSecureBuffer& password);

    void Close();
    bool IsOpen() const { return vault_.IsOpen(); }

    static bool Exists(const std::string& path) {
        return EncryptedFileStore::Exists(path);
    }

    // Validates the URI, then stores it. Rejects a duplicate key rather than
    // silently replacing: overwriting an existing account destroys a second
    // factor, so it has to be a deliberate Remove followed by an Add.
    StoreResult AddFromUri(const std::string& otpauthUri, std::string& outKey);

    StoreResult Remove(const std::string& key);

    // The single edit path. Rewrites an existing account's label *and* its OTP
    // parameters; renaming is just the case where only the label changed. Both
    // travel together because both live in the same stored URI and the label
    // also determines the key, so splitting them would mean two writes where
    // one will do.
    //
    // The seed is preserved untouched: it is read, carried across the rewrite,
    // and destroyed, never leaving this call. `outKey` reports the key the
    // account now lives under, which differs from `key` whenever the label
    // changed. Renaming onto another existing account is refused.
    //
    // Changing digits, period, algorithm or type does NOT re-negotiate
    // anything with the service — it only changes what this app computes. A
    // caller offering these must say so; getting them wrong silently produces
    // codes the server rejects.
    StoreResult Update(const std::string& key, const Otp::Parameters& newParams,
                       std::string& outKey);

    // Re-derives the vault key from a new password and rewrites the file.
    // `currentPassword` is verified first, by opening the vault file afresh:
    // the vault being unlocked proves only that somebody knew the password at
    // launch, which is not the same as the person asking to change it now.
    StoreResult ChangePassword(const UltraCryptSecureBuffer& currentPassword,
                               const UltraCryptSecureBuffer& newPassword);

    // Hands back the account's otpauth:// URI, which carries the seed. Gated
    // on `password` matching the vault's. See the exception note at the top of
    // this file — this is the one method that lets a secret out.
    StoreResult Reveal(const std::string& key,
                       const UltraCryptSecureBuffer& password,
                       UltraCryptSecureBuffer& outUri) const;

    // Accounts in stable (sorted-by-key) order. Never touches a seed.
    StoreResult List(std::vector<Account>& outAccounts) const;

    // Current TOTP code for `key` at `unixTime`, plus the seconds left in the
    // step so the UI can render a countdown. The seed lives only inside this
    // call.
    StoreResult GenerateTotp(const std::string& key, int64_t unixTime,
                             std::string& outCode,
                             uint32_t& outSecondsRemaining) const;

    // Current HOTP code, then increments and persists the counter. Separate
    // from GenerateTotp because it is a mutation: an HOTP counter must advance
    // exactly once per code handed to the user, and persist before it is
    // shown, or a crash re-issues a used code.
    StoreResult AdvanceHotp(const std::string& key, std::string& outCode);

    size_t Count() const { return vault_.Count(); }

private:
    // "issuer:accountName", or just the account name when there is no issuer —
    // the same shape as the otpauth:// label, so the key stays human-readable
    // in any future export.
    static std::string MakeKey(const Otp::Parameters& params);

    // Reads and reparses one entry. Both outputs are filled on success; the
    // secret buffer is the caller's to keep only as long as it needs.
    StoreResult LoadEntry(const std::string& key, Otp::Parameters& outParams,
                          UltraCryptSecureBuffer& outSecret) const;

    // Kept so ChangePassword and Reveal can re-open the file to verify a
    // password, rather than trusting that the session is still the same person.
    std::string path_;

    mutable EncryptedFileStore vault_;
};

} // namespace Authenticator
} // namespace UltraCanvas

#endif // ACCOUNTSTORE_H
