// Apps/UltraAuthenticator/AccountStore.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "AccountStore.h"

#include "otp/OtpAuthUri.h"

#include <algorithm>
#include <cstring>

namespace UltraCanvas {
namespace Authenticator {

namespace {

// Wraps a URI string in a secure buffer for storage, wiping the source. The
// URI carries the Base32 secret, so it is exactly as sensitive as the seed and
// must not sit in a plain std::string any longer than necessary.
UltraCryptSecureBuffer AdoptUri(std::string& uri) {
    return UltraCryptSecureBuffer::AdoptString(uri);
}

// Copies a stored value back out as a string for parsing, then the caller
// wipes it. Kept in one place so every call site wipes the same way.
std::string CopyOutUri(const UltraCryptSecureBuffer& value) {
    return std::string(reinterpret_cast<const char*>(value.Data()),
                       value.GetSize());
}

void WipeString(std::string& s) {
    if (!s.empty()) UltraCrypt_SecureZero(&s[0], s.size());
    s.clear();
}

} // namespace

std::string AccountStore::MakeKey(const Otp::Parameters& params) {
    std::string key = params.issuer.empty()
                          ? params.accountName
                          : params.issuer + ":" + params.accountName;
    if (key.size() > kMaxAccountKeyLength) key.resize(kMaxAccountKeyLength);
    return key;
}

StoreResult AccountStore::Create(const std::string& path,
                                 const UltraCryptSecureBuffer& password) {
    StoreResult created = vault_.Create(path, password);
    if (created) path_ = path;
    return created;
}

StoreResult AccountStore::Open(const std::string& path,
                               const UltraCryptSecureBuffer& password) {
    StoreResult opened = vault_.Open(path, password);
    if (opened) path_ = path;
    return opened;
}

void AccountStore::Close() {
    vault_.Close();
    path_.clear();
}

StoreResult AccountStore::AddFromUri(const std::string& otpauthUri,
                                     std::string& outKey) {
    outKey.clear();
    if (!vault_.IsOpen()) {
        return StoreResult::Error(StoreResultCode::NotOpen, "no vault is open");
    }

    // Validate before storing. A URI that cannot be parsed back is a URI that
    // silently loses an account, so parsing is the gate for writing.
    Otp::Parameters params;
    UltraCryptSecureBuffer secret;
    Otp::Result parsed = Otp::ParseOtpAuthUri(otpauthUri, params, secret);
    if (!parsed) {
        return StoreResult::Error(StoreResultCode::InvalidArgument,
                                  parsed.message);
    }

    const std::string key = MakeKey(params);
    if (key.empty()) {
        return StoreResult::Error(StoreResultCode::InvalidArgument,
                                  "account has no label");
    }

    // Refuse to clobber. Replacing an account silently destroys a second
    // factor; the caller must Remove first and mean it.
    UltraCryptSecureBuffer existing;
    if (vault_.Get(key, existing)) {
        return StoreResult::Error(StoreResultCode::AlreadyExists,
                                  "an account named '" + key + "' already exists");
    }

    // Re-build the URI from the validated parameters rather than storing the
    // caller's bytes verbatim: what goes in the vault is then always in our
    // own canonical form, with no unknown parameters or odd encoding riding
    // along to be replayed later.
    std::string canonical;
    Otp::Result built = Otp::BuildOtpAuthUri(params, secret, canonical);
    if (!built) {
        WipeString(canonical);
        return StoreResult::Error(StoreResultCode::InternalError, built.message);
    }

    UltraCryptSecureBuffer value = AdoptUri(canonical);
    StoreResult put = vault_.Put(key, value);
    if (!put) return put;

    outKey = key;
    return StoreResult::Ok();
}

StoreResult AccountStore::Remove(const std::string& key) {
    return vault_.Delete(key);
}

StoreResult AccountStore::Update(const std::string& key,
                                 const Otp::Parameters& newParams,
                                 std::string& outKey) {
    outKey.clear();
    if (!vault_.IsOpen()) {
        return StoreResult::Error(StoreResultCode::NotOpen, "no vault is open");
    }

    // Load first: this both proves the account exists and yields the seed,
    // which the rewrite has to carry across unchanged.
    Otp::Parameters current;
    UltraCryptSecureBuffer secret;
    StoreResult loaded = LoadEntry(key, current, secret);
    if (!loaded) return loaded;

    const std::string newKey = MakeKey(newParams);
    if (newKey.empty()) {
        return StoreResult::Error(StoreResultCode::InvalidArgument,
                                  "account has no label");
    }

    // BuildOtpAuthUri is the validator, exactly as on the add path: parameters
    // that could not be written back out are parameters that would lose the
    // account, so they are rejected before anything is written.
    std::string canonical;
    Otp::Result built = Otp::BuildOtpAuthUri(newParams, secret, canonical);
    if (!built) {
        WipeString(canonical);
        return StoreResult::Error(StoreResultCode::InvalidArgument,
                                  built.message);
    }

    UltraCryptSecureBuffer value = AdoptUri(canonical);
    StoreResult replaced = vault_.Replace(key, newKey, value);
    if (!replaced) return replaced;

    outKey = newKey;
    return StoreResult::Ok();
}

StoreResult AccountStore::ChangePassword(
    const UltraCryptSecureBuffer& currentPassword,
    const UltraCryptSecureBuffer& newPassword) {
    if (!vault_.IsOpen()) {
        return StoreResult::Error(StoreResultCode::NotOpen, "no vault is open");
    }
    if (newPassword.GetSize() == 0) {
        return StoreResult::Error(StoreResultCode::InvalidArgument,
                                  "the new password is empty");
    }

    // Verify the current password against the file rather than against the
    // open session. Being unlocked says somebody knew the password at launch;
    // it does not say the person asking for a change does.
    {
        EncryptedFileStore probe;
        StoreResult opened = probe.Open(path_, currentPassword);
        if (!opened) {
            return StoreResult::Error(StoreResultCode::AuthenticationFailed,
                                      "That password is not correct.");
        }
        probe.Close();
    }

    return vault_.ChangePassword(newPassword);
}

StoreResult AccountStore::Reveal(const std::string& key,
                                 const UltraCryptSecureBuffer& password,
                                 UltraCryptSecureBuffer& outUri) const {
    outUri.Clear();
    if (!vault_.IsOpen()) {
        return StoreResult::Error(StoreResultCode::NotOpen, "no vault is open");
    }

    // Same reasoning as ChangePassword: re-authenticate against the file. This
    // is the gate that makes handing a seed back defensible at all, so it runs
    // before the entry is even looked up — a wrong password must not be able
    // to learn whether an account exists.
    {
        EncryptedFileStore probe;
        StoreResult opened = probe.Open(path_, password);
        if (!opened) {
            return StoreResult::Error(StoreResultCode::AuthenticationFailed,
                                      "That password is not correct.");
        }
        probe.Close();
    }

    return vault_.Get(key, outUri);
}

StoreResult AccountStore::LoadEntry(const std::string& key,
                                    Otp::Parameters& outParams,
                                    UltraCryptSecureBuffer& outSecret) const {
    UltraCryptSecureBuffer value;
    StoreResult got = vault_.Get(key, value);
    if (!got) return got;

    std::string uri = CopyOutUri(value);
    Otp::Result parsed = Otp::ParseOtpAuthUri(uri, outParams, outSecret);
    WipeString(uri);
    if (!parsed) {
        // The vault authenticated this value, so a parse failure here is not
        // tampering — it is a URI we wrote and can no longer read.
        return StoreResult::Error(StoreResultCode::Corrupt,
                                  "stored account '" + key +
                                      "' is unreadable: " + parsed.message);
    }
    return StoreResult::Ok();
}

StoreResult AccountStore::List(std::vector<Account>& outAccounts) const {
    outAccounts.clear();
    if (!vault_.IsOpen()) {
        return StoreResult::Error(StoreResultCode::NotOpen, "no vault is open");
    }

    std::vector<std::string> keys;
    StoreResult listed = vault_.List(keys);
    if (!listed) return listed;

    std::sort(keys.begin(), keys.end());
    outAccounts.reserve(keys.size());
    for (const std::string& key : keys) {
        Account account;
        account.key = key;
        UltraCryptSecureBuffer secret;      // destroyed at the end of each turn
        StoreResult loaded = LoadEntry(key, account.params, secret);
        if (!loaded) return loaded;
        outAccounts.push_back(std::move(account));
    }
    return StoreResult::Ok();
}

StoreResult AccountStore::GenerateTotp(const std::string& key, int64_t unixTime,
                                       std::string& outCode,
                                       uint32_t& outSecondsRemaining) const {
    outCode.clear();
    outSecondsRemaining = 0;
    if (!vault_.IsOpen()) {
        return StoreResult::Error(StoreResultCode::NotOpen, "no vault is open");
    }

    Otp::Parameters params;
    UltraCryptSecureBuffer secret;
    StoreResult loaded = LoadEntry(key, params, secret);
    if (!loaded) return loaded;

    if (params.type != Otp::Type::Totp) {
        return StoreResult::Error(StoreResultCode::InvalidArgument,
                                  "'" + key + "' is a counter-based (HOTP) "
                                  "account; use AdvanceHotp");
    }

    Otp::Result generated = Otp::GenerateTotp(secret, params, unixTime, outCode);
    if (!generated) {
        return StoreResult::Error(StoreResultCode::InternalError,
                                  generated.message);
    }
    outSecondsRemaining = Otp::SecondsRemaining(unixTime, params.periodSeconds);
    return StoreResult::Ok();
}

StoreResult AccountStore::AdvanceHotp(const std::string& key,
                                      std::string& outCode) {
    outCode.clear();
    if (!vault_.IsOpen()) {
        return StoreResult::Error(StoreResultCode::NotOpen, "no vault is open");
    }

    Otp::Parameters params;
    UltraCryptSecureBuffer secret;
    StoreResult loaded = LoadEntry(key, params, secret);
    if (!loaded) return loaded;

    if (params.type != Otp::Type::Hotp) {
        return StoreResult::Error(StoreResultCode::InvalidArgument,
                                  "'" + key + "' is a time-based (TOTP) "
                                  "account; use GenerateTotp");
    }

    std::string code;
    Otp::Result generated = Otp::GenerateHotp(secret, params, params.counter,
                                              code);
    if (!generated) {
        return StoreResult::Error(StoreResultCode::InternalError,
                                  generated.message);
    }

    // Persist the advanced counter *before* handing the code back. If the
    // process dies between the two, the user sees no code and the counter has
    // moved — annoying but safe. The other order re-issues a spent code after
    // a crash, which is the failure that matters.
    Otp::Parameters advanced = params;
    advanced.counter = params.counter + 1;

    std::string canonical;
    Otp::Result built = Otp::BuildOtpAuthUri(advanced, secret, canonical);
    if (!built) {
        WipeString(canonical);
        return StoreResult::Error(StoreResultCode::InternalError, built.message);
    }

    UltraCryptSecureBuffer value = AdoptUri(canonical);
    StoreResult put = vault_.Put(key, value);
    if (!put) return put;

    outCode = code;
    return StoreResult::Ok();
}

} // namespace Authenticator
} // namespace UltraCanvas
