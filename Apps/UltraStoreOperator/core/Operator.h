// Apps/UltraStoreOperator/core/Operator.h
// The operator's side of the marketplace service (Server/UltraStoreMarket;
// the store's README, *The operator tier*): the merchants and their
// standing, the review of the submissions and the publish that releases
// them, the reports with their reporter, the notices and the strikes, the
// terms and the enforcement policy, the audit log. Every request carries
// the operator token ("Authorization: Bearer <token>") the service was
// started with. The HTTP call is the one the merchant backend's Market.h
// hands around (MarketCall), so the tests use a fake and nothing here
// touches the network in a test; the default is UltraNet.
//
// The settings - the service's URL, the token, the operator's name - are
// the program's own file (settings.json under the data folder, readable by
// the user alone), overridden by ULTRASTORE_OPERATOR_URL and _TOKEN; the
// window and the command line read the same file.
// Version: 0.5.0 - the second factor and the password reset by mail; the build downloaded from the package store
// Version: 0.4.0 - the operator accounts with their roles: the sign-in, the session in the settings, the account service on the same session
// Version: 0.3.0 - the applications for a shop, the merchant's website and description
// Version: 0.2.0 - the users of the account service: the records, the calls with the account token
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "Market.h"
#include "UltraStoreTypes.h"

#include "DataFormats/UltraCanvasJSON.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace UltraStoreOperator {

using UltraStore::ErrorCode;
using UltraStore::Result;
using UltraStoreAdmin::MarketCall;
using UltraStoreAdmin::MarketReply;
using UltraStoreAdmin::UltraNetMarketCall;

// ===========================================================================
// The settings
// ===========================================================================
struct OperatorSettings {
    std::string url;     // the service's base, https://market.example
    // The bearer: the service's static ULTRASTORE_MARKET_OPERATOR_TOKEN (the
    // bootstrap admin), or the session a sign-in gave ("op1."...), which
    // names the operator and the role and ends after the service's hours.
    std::string token;
    std::string name;    // who sits at the window: the account's name after a sign-in; shown, and the assignee of a report
    std::string role;    // the signed-in account's role (admin, reviewer, support, auditor); empty with the static token
    // The account service (Server/UltraStoreAccount) the Users page drives.
    // Its own operator token (ULTRASTORE_ACCOUNT_OPERATOR_TOKEN) when set;
    // otherwise the session, which the account service verifies with the
    // secret the two services share.
    std::string accountUrl, accountToken;
    bool SignedIn() const { return token.rfind("op1.", 0) == 0; }
    std::string AccountBearer() const { return !accountToken.empty() ? accountToken : SignedIn() ? token : std::string(); }
    bool Accounts() const { return !accountUrl.empty() && !AccountBearer().empty(); }

    static std::string DefaultPath();                           // <data folder>/UltraStoreOperator/settings.json
    static Result<OperatorSettings> Load(const std::string& pathUtf8);   // empty settings when the file is not there
    Result<void> Save(const std::string& pathUtf8) const;               // the folder made; the file the user's alone
    // The environment over the file: ULTRASTORE_OPERATOR_URL, _TOKEN, _NAME,
    // _ACCOUNT_URL, _ACCOUNT_TOKEN.
    OperatorSettings WithEnvironment() const;
    bool Complete() const { return !url.empty() && !token.empty(); }
};

// ===========================================================================
// The records, as the service's public() shapes them
// ===========================================================================
struct Merchant {
    std::string id, name, contact, key, createdAt;
    std::string website, description;   // the merchant's own profile (the catalog's publisher page)
    std::string standing;   // active, paused (the terms), suspended
    int strikes = 0;
    int termsInForce = 0, termsAccepted = 0;
    bool TermsDue() const { return termsInForce > termsAccepted; }
};

// A merchant's application for a shop (POST /v1/merchants/apply), decided by the operator.
struct MerchantApplication {
    std::string id, name, contact, email, website, description, key, state, message, createdAt, decidedAt, decidedBy, merchant;
    bool Open() const { return state == "applied"; }
};

struct Suspension {
    int64_t id = 0;
    std::string scope, target, clause, reason, start, end, liftedAt, createdBy;
    bool Lifted() const { return !liftedAt.empty(); }
};

struct Acceptance {
    std::string merchant, acceptedBy, acceptedAt;
    int version = 0;
};

struct Decision {
    std::string action, message, actor, at;
    int sequence = 0;
};

struct Submission {
    std::string id, merchant, app, version, state, answers, createdAt, decidedAt;
    int releasedSequence = 0;
    std::vector<std::pair<std::string, std::string>> checks;   // name -> ok / failed / not run / a standing
    std::vector<Decision> decisions;
    UltraCanvas::JSONValue component, packages;
    bool ChecksFailed() const;
    std::string Name() const;   // the component's name, the app id when none
};

struct MerchantFile {
    Merchant merchant;
    std::string notes;
    std::vector<Suspension> suspensions;
    std::vector<Acceptance> acceptances;
    std::vector<Submission> submissions;
    bool Suspended() const;
};

struct Report {
    std::string id, source, targetKind, target, kind, text, state, createdAt, resolvedAt, outcome;
    std::string reporter, reporterMerchant, assignee, findings;   // the operator's to see
    std::vector<std::string> attachments;
    bool Open() const { return state != "resolved" && state != "dismissed"; }
};

struct Notice {
    std::string id, kind, subject, text, clause, createdAt, createdBy, expiresAt, deadline;
    std::vector<std::string> recipients;   // merchant ids, or "*"
};

struct NoticeReceipt {
    Notice notice;
    std::vector<Suspension> suspended;   // the merchants the policy suspended on it
};

struct TermsVersion {
    int version = 0;
    std::string text, changes, inForceFrom, createdAt, createdBy;
};

struct TermsList {
    std::vector<TermsVersion> versions;
    int inForce = 0;
};

struct Policy {
    int strikeExpiryDays = 365;
    int strikesBeforeSuspension = 3;
    int suspensionDays = 30;
    int answerDeadlineDays = 14;
    std::vector<std::string> clausesThatSuspend;
};

struct AuditRow {
    int64_t id = 0;
    std::string at, actor, action, targetKind, target, clause, actedOn, hash;
    UltraCanvas::JSONValue before, after;
    std::string Line() const;   // one line, as the dashboard's "Operator actions" shows it
};

struct AuditPage {
    std::vector<AuditRow> rows;
    int64_t next = 0;
};

// ----- the operator accounts (the marketplace service keeps them) -----
struct OperatorAccount {
    std::string id, email, name, role, createdAt, createdBy, disabledAt, lastSignin;
    bool active = true;
    bool twoFactor = false;
    int recoveryCodesLeft = 0;
    std::string Name() const { return name.empty() ? email : name; }
};

// Who the bearer is, as the service sees it: the static token is the
// bootstrap admin ("operator", bootstrap), a session the account and its role.
struct WhoAmI {
    std::string id, name, role;
    bool bootstrap = false;
    std::vector<std::string> permissions;   // beyond reading: merchants.manage, review.decide, publish, reports.handle, notices.send, terms.manage, policy.manage, users.support, users.manage, operators.manage
    bool May(const std::string& permission) const;
};

struct OperatorSignIn {
    std::string token;   // the session, kept as the settings' token
    OperatorAccount account;
    int hours = 0;       // how long the session lasts
};

// The authenticator's setup: the secret to type, or the URL a QR code
// carries (otpauth://totp/...), as UltraAuthenticator takes them.
struct TwoFactorSetup {
    std::string secret, otpauthUrl;
};

// A package the merchant uploaded, as the review downloads it.
struct DownloadedPackage {
    std::string sha256, fileUtf8;
    uint64_t size = 0;
};

// A file transfer: GET `url` with `authorization` into the file at
// destinationUtf8 (a .part beside it while it comes). The default is
// UltraNet's streamed download; the tests use a fake.
using DownloadCall = std::function<MarketReply(const std::string& url, const std::string& authorization, const std::string& destinationUtf8)>;
DownloadCall UltraNetDownloadCall();
// The name a submission's package is kept under: <sha256>.<format>.
std::string PackageFileName(const UltraCanvas::JSONValue& package);

inline const char* const kRoles[] = {"admin", "reviewer", "support", "auditor"};

// ----- the account service's users (its operator API) -----
struct User {
    std::string id, email, displayName, handle, language, country, state, createdAt;   // the email masked by the service
    std::string deletionRequestedAt, deleteAfter, suspendedReason, suspendedUntil;
    std::string request;    // "", "archive", "deletion" - what the person asked of the operator (the list)
    bool emailVerified = false, twoFactor = false;
    int devices = 0, licences = 0;   // counted (the list)
    std::string Name() const { return displayName.empty() ? (handle.empty() ? email : "@" + handle) : displayName; }
};

struct UserStats {
    int accounts = 0, newIn30Days = 0, twoFactorPercent = 0, archiveRequests = 0, archiveRequestsDue = 0, deletionRequests = 0, suspended = 0;
};

struct UsersPage {
    std::vector<User> users;
    UserStats stats;
};

struct UserDevice {
    std::string id, name, os, client, lastSeen, lastIp;
};

struct UserLicence {
    std::string id, app, order, issued, validUntil;
    int seats = 0, seatsUsed = 0;
};

struct UserNotice {
    std::string id, kind, subject, text, createdAt, createdBy, readAt;
};

struct UserArchive {
    std::string id, state, due, requestedAt, releasedAt;
};

struct UserSignIn {
    std::string at, ip, device, reason;
    bool ok = false;
};

struct UserFile {
    User user;
    std::vector<UserDevice> devices;
    std::vector<UserLicence> licences;
    std::vector<UserNotice> notices;
    std::vector<UserArchive> archives;
    std::vector<AuditRow> history;
    std::vector<UserSignIn> signIns;
};

struct AuditCheck {
    bool ok = false;
    int64_t rows = 0, brokenAt = 0;
    std::string lastHash;
};

struct ReleasedEntry {
    std::string app, merchant, submission;
    int sequence = 0;
    UltraCanvas::JSONValue component, packages;
};

struct Released {
    std::vector<ReleasedEntry> entries;
    int sequence = 0;    // the last release's
    int published = 0;   // the last publish's: a release past it waits for the next publish
};

struct PublishReport {
    int sequence = 0, apps = 0, components = 0, publishers = 0;
    std::vector<std::string> leftOut, files;
    std::string publicKey;
};

struct PublishKey {
    std::string publicKey;
    std::vector<std::string> trustedKeys;
};

inline const char* const kNoticeKinds[] = {"announcement", "reminder", "information_request", "warning", "suspension"};
inline const char* const kDecisions[] = {"start_review", "release", "request_changes", "reject"};

// "3 d", "6 h", "12 min" since an ISO-8601 UTC time the service wrote
// (2026-11-11T18:00:00Z); empty when it does not parse. `now` for the tests.
std::string Age(const std::string& at, int64_t now = 0);

// ===========================================================================
// The client
// ===========================================================================
class OperatorClient {
public:
    explicit OperatorClient(OperatorSettings settings, MarketCall call = UltraNetMarketCall(), DownloadCall download = UltraNetDownloadCall());

    // ----- the service -----
    Result<std::string> Health() const;   // GET /healthz: "ok", or what is not configured

    // ----- the operator accounts -----
    // The sign-in needs the URL only: the session it gives is the token from
    // then on. `code` is the authenticator's six digits, or a recovery code,
    // when the account has a second factor; without one the failure's
    // detail is "two_factor_required".
    Result<OperatorSignIn> SignIn(const std::string& email, const std::string& password, const std::string& code = {}) const;
    // One's own second factor: the setup (the secret for the app), enabled
    // with the app's first code (the recovery codes come back once),
    // disabled with a code, a new set of recovery codes with a code.
    Result<TwoFactorSetup> TwoFactorSetup() const;
    Result<std::vector<std::string>> TwoFactorEnable(const std::string& code) const;
    Result<void> TwoFactorDisable(const std::string& code) const;
    Result<std::vector<std::string>> NewRecoveryCodes(const std::string& code) const;
    // A forgotten password: a code mailed to the account (needs the URL only),
    // then the reset with it; `code2` the second factor when the account has one.
    Result<int> ForgotPassword(const std::string& email) const;   // the minutes the code lives
    Result<void> ResetPassword(const std::string& email, const std::string& code, const std::string& newPassword, const std::string& code2 = {}) const;
    // An admin resets another operator's second factor, after the identity was checked another way.
    Result<OperatorAccount> ResetOperatorTwoFactor(const std::string& id) const;
    Result<WhoAmI> Me() const;
    Result<std::vector<OperatorAccount>> Operators() const;                                 // admin, and anyone reads
    Result<OperatorAccount> AddOperator(const std::string& email, const std::string& name, const std::string& role, const std::string& password) const;
    // An empty field is left as it is; disabled -1 leaves the switch, 0 enables, 1 disables.
    // Anyone changes their own name and password; the role and the switch are an admin's.
    Result<OperatorAccount> UpdateOperator(const std::string& id, const std::string& name, const std::string& role, const std::string& password, int disabled = -1) const;

    // ----- merchants -----
    Result<std::vector<Merchant>> Merchants() const;
    Result<MerchantFile> GetMerchant(const std::string& id) const;
    // id empty: the service makes one. key: the public half, 32 bytes hex.
    Result<Merchant> RegisterMerchant(const std::string& id, const std::string& name, const std::string& contact, const std::string& keyHex) const;
    // An empty field is left as it is; a key given is the rotation.
    Result<Merchant> UpdateMerchant(const std::string& id, const std::string& name, const std::string& contact, const std::string& keyHex) const;
    // ----- the applications for a shop -----
    Result<std::vector<MerchantApplication>> Applications(const std::string& state = {}) const;
    Result<MerchantApplication> GetApplication(const std::string& id) const;
    // Approved: the merchant registered with the application's key and profile, under the id given or one made.
    Result<Merchant> ApproveApplication(const std::string& id, const std::string& merchantId, const std::string& message) const;
    Result<MerchantApplication> RejectApplication(const std::string& id, const std::string& message) const;   // the message is the applicant's to read
    Result<Merchant> SuspendMerchant(const std::string& id, const std::string& clause, const std::string& reason, int days) const;   // 0: until reinstated
    Result<Merchant> ReinstateMerchant(const std::string& id, const std::string& reason) const;
    Result<Suspension> SuspendApp(const std::string& appId, const std::string& clause, const std::string& reason, int days) const;
    Result<int> ReinstateApp(const std::string& appId, const std::string& reason) const;   // the suspensions lifted

    // ----- the review -----
    Result<std::vector<Submission>> Submissions(const std::string& state = {}, const std::string& merchant = {}, const std::string& app = {}) const;
    Result<Submission> GetSubmission(const std::string& id) const;
    // action: start_review, release (sequence 0: the next), request_changes, reject.
    Result<Submission> Decide(const std::string& id, const std::string& action, const std::string& message, int sequence = 0) const;
    // A package the merchant uploaded into the package store, into the
    // folder, under <sha256>.<format> (the review's Download build).
    Result<DownloadedPackage> DownloadPackage(const std::string& submissionId, const UltraCanvas::JSONValue& package, const std::string& folderUtf8) const;
    Result<Released> GetReleased() const;
    Result<PublishReport> Publish() const;
    Result<PublishKey> GetPublishKey() const;

    // ----- reports -----
    Result<std::vector<Report>> Reports(const std::string& state = {}) const;
    Result<Report> GetReport(const std::string& id) const;
    // A report the operator enters from mail: targetKind app, release or merchant.
    Result<Report> OpenReport(const std::string& targetKind, const std::string& target, const std::string& kind, const std::string& text,
                              const std::string& reporter) const;
    Result<Report> AssignReport(const std::string& id, const std::string& assignee) const;
    // resolve true: upheld; false: dismissed. The outcome is the reporter's one line.
    Result<Report> CloseReport(const std::string& id, bool resolve, const std::string& findings, const std::string& outcome) const;

    // ----- notices -----
    Result<std::vector<Notice>> Notices() const;
    // recipients: merchant ids, or {"*"} for everyone; a warning names its clause.
    Result<NoticeReceipt> SendNotice(const std::string& kind, const std::string& subject, const std::string& text, const std::string& clause,
                                     const std::vector<std::string>& recipients) const;

    // ----- terms and policy -----
    Result<TermsList> Terms() const;
    Result<TermsVersion> AddTerms(const std::string& text, const std::string& changes, const std::string& inForceFrom, int version = 0) const;
    Result<Policy> GetPolicy() const;
    Result<Policy> SetPolicy(const Policy& policy) const;

    // ----- the audit log -----
    Result<AuditPage> Audit(int64_t since = 0, int limit = 500) const;
    Result<AuditCheck> VerifyAudit() const;

    // ----- the users (the account service, with its own token) -----
    Result<std::string> AccountHealth() const;
    // state: "", active, suspended, deleting, requests; query: a part of the email, handle or name.
    Result<UsersPage> Users(const std::string& state = {}, const std::string& query = {}) const;
    Result<UserFile> GetUser(const std::string& id) const;
    Result<UserStats> GetUserStats() const;
    // kind: notice, security, warning, suspension, information_request.
    Result<UserNotice> NotifyUser(const std::string& id, const std::string& kind, const std::string& subject, const std::string& text) const;
    Result<int> SignOutUser(const std::string& id, const std::string& reason) const;            // the devices signed out
    Result<void> ResetUserTwoFactor(const std::string& id, const std::string& reason) const;    // after the identity was checked
    Result<User> SuspendUser(const std::string& id, const std::string& reason, int days) const;  // 0: until reinstated
    Result<User> ReinstateUser(const std::string& id, const std::string& reason) const;
    Result<void> DeleteUser(const std::string& id, const std::string& reason) const;            // only a deletion the person asked for
    Result<UserArchive> ReleaseArchive(const std::string& archiveId) const;
    Result<std::pair<int, int>> AccountMaintenance() const;                                     // deleted, reinstated
    Result<AuditPage> AccountAudit(int64_t since = 0, int limit = 500) const;
    Result<AuditCheck> VerifyAccountAudit() const;

    const OperatorSettings& Settings() const { return settings_; }

private:
    Result<UltraCanvas::JSONValue> Ask(const std::string& method, const std::string& path, const std::string& jsonBody = {}) const;
    Result<UltraCanvas::JSONValue> Open(const std::string& path, const std::string& jsonBody) const;   // a POST without a bearer
    Result<UltraCanvas::JSONValue> AskAccount(const std::string& method, const std::string& path, const std::string& jsonBody = {}) const;

    OperatorSettings settings_;
    MarketCall call_;
    DownloadCall download_;
};

// The parsers, for the window's own reading of a service answer.
Merchant MerchantOf(const UltraCanvas::JSONValue& v);
MerchantApplication ApplicationOf(const UltraCanvas::JSONValue& v);
Submission SubmissionOf(const UltraCanvas::JSONValue& v);
Report ReportOf(const UltraCanvas::JSONValue& v);
Notice NoticeOf(const UltraCanvas::JSONValue& v);
Suspension SuspensionOf(const UltraCanvas::JSONValue& v);
AuditRow AuditRowOf(const UltraCanvas::JSONValue& v);
User UserOf(const UltraCanvas::JSONValue& v);
UserFile UserFileOf(const UltraCanvas::JSONValue& v);
UserStats UserStatsOf(const UltraCanvas::JSONValue& v);
Policy PolicyOf(const UltraCanvas::JSONValue& v);
UltraCanvas::JSONValue PolicyJson(const Policy& p);

} // namespace UltraStoreOperator
