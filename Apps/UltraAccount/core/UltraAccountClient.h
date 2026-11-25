// Apps/UltraAccount/core/UltraAccountClient.h
// The client side of UltraAccount - the one ULTRA OS account a person signs
// in with - for every application that needs it: the UltraAccount app
// (registration, the email and phone verified, the subscription), and the
// applications that rest on it - UltraAuthenticator's recovery from a
// forgotten master password today, cloud storage and the paid AI services
// next. The service is Server/UltraStoreAccount (its README, *UltraAccount*).
//
// One sign-in serves every application on the machine: the session the
// UltraAccount app (or any application here) signs in with is kept in
// UltraAccount's data folder (Session::Path, readable by the owner only), and
// an application that needs the account reads it from there rather than
// asking for the password again.
//
// The HTTP call is a function the caller may hand in, so a test answers from
// a map and nothing here touches the network in a test; the default is
// UltraNet, HTTPS, redirects to https:// only.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef ULTRAACCOUNTCLIENT_H
#define ULTRAACCOUNTCLIENT_H

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace UltraAccount {

// The account service the project runs; a session remembers the one it
// came from, and the UltraAccount app's settings may name another.
inline constexpr const char* kDefaultServiceUrl = "https://store.ultraos.eu/account";
// The folder under the platform's application data root
// (UltraCanvasDataFolder.h) that holds the shared session.
inline constexpr const char* kDataFolderName = "UltraAccount";

// ----- the HTTP call -----
struct HttpReply {
    int status = 0;
    std::string body;
    std::string error;   // empty when the service answered, whatever it said
};
// method GET, POST, PUT or DELETE; a POST or PUT sends jsonBody as
// application/json; authorization is the whole header value or empty.
using HttpCall = std::function<HttpReply(const std::string& method, const std::string& url,
                                         const std::string& authorization, const std::string& jsonBody)>;
HttpCall UltraNetCall();

// ----- what a call returns -----
// `code` is the service's error ("two_factor_required", "subscription_required",
// "bad_code", "code_sent", "signed_out", ...), or "network" when it could not
// be reached and "parse" when its answer was not JSON; `message` is the
// service's description, for the person.
struct Error {
    std::string code;
    std::string message;
    int status = 0;
};

template <typename T>
struct Result {
    std::optional<T> value;
    Error error;
    explicit operator bool() const { return value.has_value(); }
    const T& Value() const { return *value; }
    static Result Succeed(T v) { Result r; r.value = std::move(v); return r; }
    static Result Fail(Error e) { Result r; r.error = std::move(e); return r; }
};

struct Done {};   // the value of a call that answers nothing but success

// ----- what the service describes -----
struct DeviceInfo {
    std::string name;     // the machine's host name until renamed
    std::string os;       // "linux", "windows", "macos"
    std::string client;   // "UltraAccount 0.1.0"
};
std::string DefaultDeviceName();
std::string CurrentOs();

// The plan the account is on, and what it allows. `sms` is whether codes go
// out by text message - a text costs money, so only a paid plan has it.
struct Plan {
    std::string plan = "free";
    bool paid = false;
    std::string validUntil;   // empty: until cancelled, or the free plan
    std::string source;       // "licence", "operator", or empty on the free plan
    bool sms = false;
};

struct Profile {
    std::string id, email, displayName, phone, state, createdAt;
    bool emailVerified = false;
    bool phoneVerified = false;
    bool twoFactor = false;
};

// GET /v1/me, as far as the applications here read it.
struct Me {
    Profile profile;
    Plan plan;
    int recoveryKeys = 0;
    int devices = 0;
    int unreadNotices = 0;
    std::string deviceId;
};

// Where a recovery code can be sent: "email" or "sms", and the address
// masked ("an***@example.org", "+49 *** 89").
struct Channel {
    std::string channel;
    std::string to;
};

// A recovery key an application left with the account. Never its secret.
struct RecoveryKey {
    std::string id, app, label, createdAt, releasedAt;
    int releases = 0;
};

// What anyone holding a recovery key's id may learn: whose application it
// is for, and where a code would go.
struct RecoveryKeyInfo {
    std::string id, app, label;
    std::vector<Channel> channels;
};

// ----- the session on disk -----
// What a machine keeps of being signed in: the service it came from, the
// token, who it is for, which device this machine is. In UltraAccount's data
// folder, readable by the owner alone: the token signs in as the person.
struct Session {
    std::string url, token, userId, email, deviceId;
    bool SignedIn() const { return !token.empty() && !url.empty(); }

    static std::string Path();                       // <data>/UltraAccount/session.json
    static Session Load(const std::string& path = Path());
    bool Save(const std::string& path = Path()) const;
    static void Clear(const std::string& path = Path());
};

// ----- the client -----
class Client {
public:
    explicit Client(std::string serviceUrl = kDefaultServiceUrl, HttpCall call = UltraNetCall());

    const std::string& Url() const { return url_; }

    // ----- before signing in -----
    // A code goes to the address; the same answer whether or not it had an
    // account already, so a stranger learns nothing from it.
    Result<Done> SignUp(const std::string& email, const std::string& password, const std::string& country,
                        const std::string& language) const;
    Result<Session> VerifySignUp(const std::string& email, const std::string& code, const DeviceInfo& device) const;
    Result<Done> ResendCode(const std::string& email) const;
    // Error code "two_factor_required" until `totp` (or a recovery code of
    // the account's own two-factor sign-in) comes with it.
    Result<Session> SignIn(const std::string& email, const std::string& password, const DeviceInfo& device,
                           const std::string& totp = {}) const;
    Result<Done> ForgotPassword(const std::string& email) const;
    Result<Done> ResetPassword(const std::string& email, const std::string& code, const std::string& newPassword,
                               const std::string& totp = {}) const;

    // ----- signed in -----
    Result<Me> GetMe(const std::string& token) const;
    Result<Done> SignOut(const std::string& token) const;
    Result<Plan> GetPlan(const std::string& token) const;
    // The paid plan from a payment-service licence for the subscription app.
    Result<Plan> Subscribe(const std::string& token, const std::string& licence) const;
    // A texted code to the number (paid plan only: "subscription_required");
    // the answer is the number masked.
    Result<std::string> AddPhone(const std::string& token, const std::string& phone, const std::string& password) const;
    Result<Me> VerifyPhone(const std::string& token, const std::string& code) const;
    Result<Me> RemovePhone(const std::string& token, const std::string& password) const;
    Result<std::vector<RecoveryKey>> RecoveryKeys(const std::string& token) const;
    // The key is 32 bytes as 64 hex digits; the answer has the id the
    // application keeps to ask for it again.
    Result<RecoveryKey> StoreRecoveryKey(const std::string& token, const std::string& app, const std::string& label,
                                         const std::string& secretHex) const;
    Result<Done> DeleteRecoveryKey(const std::string& token, const std::string& id) const;

    // ----- recovering a key (anyone holding its id) -----
    Result<RecoveryKeyInfo> GetRecoveryKeyInfo(const std::string& id) const;
    // The masked address the code went to.
    Result<std::string> SendRecoveryCode(const std::string& id, const std::string& channel) const;
    // The key, 64 hex digits. The caller wipes the string once it has it.
    Result<std::string> ReleaseRecoveryKey(const std::string& id, const std::string& code) const;

private:
    Result<std::string> Ask(const std::string& method, const std::string& path, const std::string& token,
                            const std::string& jsonBody) const;

    std::string url_;
    HttpCall call_;
};

// A message for the person from an error, in their language where the
// service's own description is missing.
std::string Describe(const Error& error);

} // namespace UltraAccount

#endif // ULTRAACCOUNTCLIENT_H
