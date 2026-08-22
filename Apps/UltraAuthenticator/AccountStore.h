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
// The security point of this class: **the UI never sees a secret.** Callers
// get accounts (issuer, label, digits, period) and generated codes. The seed
// is read out of the vault, used, and destroyed inside a single call, so it
// exists in memory for microseconds rather than for as long as a window is
// open. Nothing here returns an UltraCryptSecureBuffer to the caller, and
// nothing here puts a seed in a std::string.
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

    mutable EncryptedFileStore vault_;
};

} // namespace Authenticator
} // namespace UltraCanvas

#endif // ACCOUNTSTORE_H
