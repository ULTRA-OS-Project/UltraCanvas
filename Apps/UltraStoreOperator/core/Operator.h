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
    std::string token;   // ULTRASTORE_MARKET_OPERATOR_TOKEN at the service
    std::string name;    // who sits at the window: shown, never sent (the service records "operator")

    static std::string DefaultPath();                           // <data folder>/UltraStoreOperator/settings.json
    static Result<OperatorSettings> Load(const std::string& pathUtf8);   // empty settings when the file is not there
    Result<void> Save(const std::string& pathUtf8) const;               // the folder made; the file the user's alone
    // The environment over the file: ULTRASTORE_OPERATOR_URL, _TOKEN, _NAME.
    OperatorSettings WithEnvironment() const;
    bool Complete() const { return !url.empty() && !token.empty(); }
};

// ===========================================================================
// The records, as the service's public() shapes them
// ===========================================================================
struct Merchant {
    std::string id, name, contact, key, createdAt;
    std::string standing;   // active, paused (the terms), suspended
    int strikes = 0;
    int termsInForce = 0, termsAccepted = 0;
    bool TermsDue() const { return termsInForce > termsAccepted; }
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
    explicit OperatorClient(OperatorSettings settings, MarketCall call = UltraNetMarketCall());

    // ----- the service -----
    Result<std::string> Health() const;   // GET /healthz: "ok", or what is not configured

    // ----- merchants -----
    Result<std::vector<Merchant>> Merchants() const;
    Result<MerchantFile> GetMerchant(const std::string& id) const;
    // id empty: the service makes one. key: the public half, 32 bytes hex.
    Result<Merchant> RegisterMerchant(const std::string& id, const std::string& name, const std::string& contact, const std::string& keyHex) const;
    // An empty field is left as it is; a key given is the rotation.
    Result<Merchant> UpdateMerchant(const std::string& id, const std::string& name, const std::string& contact, const std::string& keyHex) const;
    Result<Merchant> SuspendMerchant(const std::string& id, const std::string& clause, const std::string& reason, int days) const;   // 0: until reinstated
    Result<Merchant> ReinstateMerchant(const std::string& id, const std::string& reason) const;
    Result<Suspension> SuspendApp(const std::string& appId, const std::string& clause, const std::string& reason, int days) const;
    Result<int> ReinstateApp(const std::string& appId, const std::string& reason) const;   // the suspensions lifted

    // ----- the review -----
    Result<std::vector<Submission>> Submissions(const std::string& state = {}, const std::string& merchant = {}, const std::string& app = {}) const;
    Result<Submission> GetSubmission(const std::string& id) const;
    // action: start_review, release (sequence 0: the next), request_changes, reject.
    Result<Submission> Decide(const std::string& id, const std::string& action, const std::string& message, int sequence = 0) const;
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

    const OperatorSettings& Settings() const { return settings_; }

private:
    Result<UltraCanvas::JSONValue> Ask(const std::string& method, const std::string& path, const std::string& jsonBody = {}) const;

    OperatorSettings settings_;
    MarketCall call_;
};

// The parsers, for the window's own reading of a service answer.
Merchant MerchantOf(const UltraCanvas::JSONValue& v);
Submission SubmissionOf(const UltraCanvas::JSONValue& v);
Report ReportOf(const UltraCanvas::JSONValue& v);
Notice NoticeOf(const UltraCanvas::JSONValue& v);
Suspension SuspensionOf(const UltraCanvas::JSONValue& v);
AuditRow AuditRowOf(const UltraCanvas::JSONValue& v);
Policy PolicyOf(const UltraCanvas::JSONValue& v);
UltraCanvas::JSONValue PolicyJson(const Policy& p);

} // namespace UltraStoreOperator
