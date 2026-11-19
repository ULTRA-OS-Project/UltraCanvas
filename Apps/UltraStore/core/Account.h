// Apps/UltraStore/core/Account.h
// The store's side of the account service (Server/UltraStoreAccount, the
// store's README, *The account tier*): sign up with the mailed code, sign
// in with the password and the second factor, this machine as one of the
// account's devices, the profile and the settings, two-factor sign-in, the
// licences that follow the person from machine to machine, the notices, the
// data archive and the deletion. The session token the service hands out is
// kept in the store folder (account.json, readable by the owner only) and
// sent as a bearer token with every request. The HTTP call is a function
// the caller hands in, so the tests use a map and nothing here touches the
// network in a test; the default is UltraNet.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "Purchase.h"
#include "Registry.h"
#include "UltraStoreTypes.h"

#include "DataFormats/UltraCanvasJSON.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace UltraStore {

// An HTTP call with an Authorization header: GET, POST, PUT or DELETE; a
// POST or PUT sends jsonBody as application/json. Purchase.h's HttpCall
// cannot carry the header, which every signed-in request needs.
using AccountCall = std::function<HttpReply(const std::string& method, const std::string& url, const std::string& authorization,
                                            const std::string& jsonBody)>;
AccountCall UltraNetAccountCall();   // HTTPS, redirects to https:// only

// What the store tells the service about this machine when it signs in.
struct DeviceInfo {
    std::string name;     // "Anna's laptop": the machine's host name until renamed
    std::string os;       // "linux/x86_64 6.8.0 - ubuntu", from PlatformInfo::Describe()
    std::string client;   // "UltraStore 0.1.25"
};
// The host name as the device's name, "This machine" when none can be read.
std::string DefaultDeviceName();

// ----- what the service describes -----
struct AccountDevice {
    std::string id, name, os, client, createdAt, lastSeen, lastIp;
    bool current = false;   // the device the request came from
};

struct AccountLicence {
    std::string id, app, orderId, issued, updates, updatesUntil, validUntil, attachedAt;
    std::string token;                 // the ULIC1 licence, as the payment service issued it
    int seats = 0;                     // 0: the service's default
    int seatsUsed = 0;
    std::vector<std::string> installedOn;   // device ids with a seat
    bool InstalledOn(const std::string& deviceId) const;
};

struct AccountNotice {
    std::string id, kind, subject, text, createdAt, createdBy, readAt;
    bool Read() const { return !readAt.empty(); }
};

struct AccountArchive {
    std::string id, state, due, requestedAt, releasedAt, downloadedAt;   // state: requested, built, released, downloaded
};

struct AccountSettings {
    std::map<std::string, std::string> notifications;   // security, orders, updates, follows, replies, deals -> email, email_and_devices, devices, monthly_email, off
    bool historyOnAccount = false;
    bool publicProfile = true;
    bool marketing = false;
};

struct AccountProfile {
    std::string id, email, displayName, handle, language, country, about, state, createdAt, passwordChangedAt;
    std::string deletionRequestedAt, deleteAfter;
    int yearOfBirth = 0;
    bool emailVerified = false;
    bool twoFactor = false;
    int recoveryCodesLeft = 0;
    AccountSettings settings;
    // "AK" from the display name, else from the email.
    std::string Initials() const;
    // The display name, else the email's local part.
    std::string Name() const;
};

// GET /v1/me: everything the account page shows.
struct Me {
    AccountProfile profile;
    AccountDevice device;
    std::vector<AccountDevice> devices;
    std::vector<AccountLicence> licences;
    int unreadNotices = 0;
    std::optional<AccountArchive> archive;   // the open one
};

// What a sign-in gives: the token to keep, this device, the person.
struct SignedIn {
    std::string token;
    AccountDevice device;
    AccountProfile profile;
};

struct TwoFactorSetup {
    std::string secret, otpauthUrl;
};

// The change the profile form sends: a field left unset is not changed.
struct ProfileChange {
    std::optional<std::string> displayName, handle, language, country, about;
    std::optional<int> yearOfBirth;
};

// ----- the session on disk -----
// What the store keeps of being signed in, <store root>/account.json: the
// token, who it is for, which device this machine is, and the service it
// came from (so a store whose catalog names another service does not send
// the token there).
struct AccountSession {
    std::string url, token, userId, email, displayName, deviceId;
    bool SignedIn() const { return !token.empty() && !url.empty(); }
    static std::string Path(const std::string& storeRootUtf8);
    static AccountSession Load(const std::string& storeRootUtf8);   // empty when there is none
    Result<void> Save(const std::string& storeRootUtf8) const;    // owner-readable only
    static void Clear(const std::string& storeRootUtf8);
};

// ----- the client -----
// The error detail carries the service's error code ("two_factor_required",
// "email_unverified", "no_seat", "signed_out", ...) when it answered with
// one, else "HTTP <status>"; the message is the service's description.
class AccountClient {
public:
    explicit AccountClient(std::string serviceUrl, AccountCall call = UltraNetAccountCall());

    const std::string& Url() const { return url_; }

    // ----- before signing in -----
    // POST /v1/signup: a code goes to the address; the same answer whether
    // or not the address had an account already.
    Result<void> SignUp(const std::string& email, const std::string& password, const std::string& country, int yearOfBirth,
                        const std::string& language, bool marketing) const;
    // POST /v1/signup/verify: the address verified, this machine signed in.
    Result<SignedIn> VerifySignUp(const std::string& email, const std::string& code, const DeviceInfo& device) const;
    Result<void> ResendCode(const std::string& email) const;
    // POST /v1/signin; detail "two_factor_required" when the account has a
    // second factor and none was given.
    Result<SignedIn> SignIn(const std::string& email, const std::string& password, const DeviceInfo& device,
                            const std::string& totp = {}, const std::string& recoveryCode = {}) const;
    Result<void> ForgotPassword(const std::string& email) const;
    Result<void> ResetPassword(const std::string& email, const std::string& code, const std::string& newPassword,
                               const std::string& totp = {}, const std::string& recoveryCode = {}) const;
    Result<std::string> Health() const;

    // ----- signed in: token is the session's -----
    Result<Me> GetMe(const std::string& token) const;
    Result<Me> UpdateProfile(const std::string& token, const ProfileChange& change) const;
    Result<Me> UpdateSettings(const std::string& token, const AccountSettings& settings) const;
    Result<void> ChangeEmail(const std::string& token, const std::string& email, const std::string& password) const;   // a code to the new address
    Result<Me> VerifyEmailChange(const std::string& token, const std::string& code) const;
    Result<int> ChangePassword(const std::string& token, const std::string& current, const std::string& fresh) const;   // the other devices signed out
    Result<TwoFactorSetup> TwoFactorSetupStart(const std::string& token) const;
    Result<std::vector<std::string>> TwoFactorEnable(const std::string& token, const std::string& code) const;       // the recovery codes, shown once
    Result<void> TwoFactorDisable(const std::string& token, const std::string& code) const;
    Result<std::vector<std::string>> NewRecoveryCodes(const std::string& token, const std::string& code) const;
    Result<std::vector<AccountDevice>> Devices(const std::string& token) const;
    Result<void> RenameDevice(const std::string& token, const std::string& deviceId, const std::string& name) const;
    Result<void> SignOutDevice(const std::string& token, const std::string& deviceId) const;
    Result<void> SignOut(const std::string& token) const;
    Result<int> SignOutOthers(const std::string& token) const;
    Result<std::vector<AccountLicence>> Licences(const std::string& token) const;
    // A payment-service licence bound to the account (a renewal of one
    // attached already replaces its token); detail "licence_taken" when it
    // belongs to another account.
    Result<AccountLicence> AttachLicence(const std::string& token, const std::string& licenceToken, int seats = 0) const;
    Result<void> ReleaseLicence(const std::string& token, const std::string& licenceId) const;
    // A seat for this device; detail "no_seat" when every seat is taken.
    Result<AccountLicence> InstallLicence(const std::string& token, const std::string& licenceId) const;
    Result<AccountLicence> UninstallLicence(const std::string& token, const std::string& licenceId) const;
    Result<std::vector<AccountNotice>> Notices(const std::string& token) const;
    Result<void> MarkNoticeRead(const std::string& token, const std::string& noticeId) const;
    Result<AccountArchive> RequestArchive(const std::string& token) const;
    Result<std::vector<AccountArchive>> Archives(const std::string& token) const;
    Result<std::string> DownloadArchive(const std::string& token, const std::string& archiveId) const;   // the JSON, once
    Result<Me> RequestDeletion(const std::string& token, const std::string& password) const;
    Result<Me> CancelDeletion(const std::string& token) const;

private:
    Result<UltraCanvas::JSONValue> Ask(const std::string& method, const std::string& path, const std::string& token,
                                       const std::string& jsonBody = {}) const;
    Result<Me> MeFrom(const Result<UltraCanvas::JSONValue>& answer) const;

    std::string url_;
    AccountCall call_;
};

// The parsers, for a caller's own reading of a service answer.
AccountProfile ProfileOf(const UltraCanvas::JSONValue& v);
AccountDevice DeviceOf(const UltraCanvas::JSONValue& v);
AccountLicence AccountLicenceOf(const UltraCanvas::JSONValue& v);
AccountNotice NoticeOf(const UltraCanvas::JSONValue& v);
AccountArchive ArchiveOf(const UltraCanvas::JSONValue& v);
Me MeOf(const UltraCanvas::JSONValue& v);

// ----- the licences, kept in step -----
// What a sync did: the licences of this machine attached to the account
// (with a seat for this machine), the account's licences this machine
// lacked and now has (verified with the licence key, into the registry),
// the apps whose seats were all taken, and what failed.
struct AccountLicenceSync {
    std::vector<std::string> attached, fetched, noSeat;
    std::vector<std::pair<std::string, Error>> failed;
    bool Changed() const { return !attached.empty() || !fetched.empty(); }
};
// Every licence in the registry goes up to the account and takes a seat
// for this machine; every licence on the account the registry lacks comes
// down, verified against licenceKeyHex (empty: none fetched), and is kept
// as a record without an order secret (the order is the account's; the
// payment service's receipt and cancel need the secret, the app does not).
Result<AccountLicenceSync> SyncLicencesWithAccount(SqliteRegistry& registry, const AccountClient& client, const std::string& token,
                                                   const std::string& licenceKeyHex);

} // namespace UltraStore
