// Apps/UltraStoreAdmin/core/Market.h
// The merchant backend's side of the marketplace service
// (Server/UltraStoreMarket; the store's README, *The operator tier*): a
// submission sent signed with this backend's key, the notices the operator
// sent and their acknowledgement, the terms and their acceptance, the
// merchant's standing and the reports about them.
//
// Every request is signed with the merchant's Ed25519 key - the key that
// signs this backend's catalog - as the service's auth.py verifies it:
// "Authorization: Merchant <id>:<timestamp>:<hex signature>", the
// signature over "<METHOD> <path>\n<timestamp>\n" and the body bytes. A
// submission carries the component as PutComponent reads it back and a
// signature over its canonical JSON (keys sorted, no spaces, UTF-8 as it
// is), which is what the service checks. The HTTP call is a function the
// caller hands in, so the tests use a fake and nothing here touches the
// network in a test; the default is UltraNet.
// Version: 0.3.0 - the packages uploaded with a submission into the operator's package store
// Version: 0.2.0 - the application for a shop, the merchant's own profile and key rotation
// Version: 0.1.1 - MarketCall speaks PUT as well (the operator's client shares it)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "AdminDatabase.h"
#include "UltraStoreTypes.h"

#include "DataFormats/UltraCanvasJSON.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace UltraStoreAdmin {

using UltraStore::ErrorCode;
using UltraStore::Result;

// The JSON text a signature covers: members by key, no whitespace, the
// escapes JSON requires and no other (the service's canonical()).
std::string CanonicalJson(const UltraCanvas::JSONValue& value);

// The Authorization header of one request (auth.py, sign_request); the
// timestamp is seconds since the epoch, now when empty.
std::string SignRequest(const std::vector<uint8_t>& seed, const std::string& merchantId, const std::string& method,
                        const std::string& path, const std::string& body, const std::string& timestamp = {});

// What an HTTP call gives back: the status (0 when nothing came) and the body.
struct MarketReply {
    int status = 0;
    std::string body;
    std::string error;
};
// method "GET", "POST" or "PUT"; `authorization` the header's value; a POST
// or a PUT sends jsonBody as application/json.
using MarketCall = std::function<MarketReply(const std::string& method, const std::string& url, const std::string& authorization,
                                             const std::string& jsonBody)>;
MarketCall UltraNetMarketCall();   // HTTPS, redirects to https:// only

// The settings the client reads: market.url (the service's base),
// market.merchant_id (the id the operator registered this backend under).
struct MarketSettings {
    std::string url, merchantId;
    std::string applicationId, applicationSecret;   // market.application_id, market.application_secret: a shop asked for, not yet decided
    static MarketSettings From(const AdminDatabase& db);
    bool Complete() const { return !url.empty() && !merchantId.empty(); }
    bool Applied() const { return !applicationId.empty() && !applicationSecret.empty(); }
};

// A notice as the merchant sees it.
struct Notice {
    std::string id, kind, subject, text, clause, createdAt, deadline, acknowledgedAt;
    bool Acknowledged() const { return !acknowledgedAt.empty(); }
};

// The merchant's standing and profile, from GET /v1/merchants/me.
struct Standing {
    std::string id, name, standing;   // active, paused, suspended
    std::string contact, website, description, key;   // the profile, the merchant's own to edit
    int strikes = 0;
    int termsInForce = 0, termsAccepted = 0;
    bool TermsDue() const { return termsInForce > termsAccepted; }
};

// The change the profile form sends: a field left unset is not changed; a
// key given is the rotation (the request is signed with the old one).
struct ProfileChange {
    std::optional<std::string> name, contact, website, description, keyHex;
};

// A shop asked for (POST /v1/merchants/apply) and what became of it.
struct ApplicationReceipt {
    std::string id, secret;
};
struct Application {
    std::string id, state, message, merchant, name, createdAt, decidedAt;   // state: applied, approved, rejected
    bool Approved() const { return state == "approved"; }
};

// The current terms, from GET /v1/terms/current.
struct Terms {
    int version = 0;
    std::string text, changes, inForceFrom;
};

// What a submission came back as.
struct SubmissionReceipt {
    std::string id, app, version, state;
    std::vector<std::pair<std::string, std::string>> checks;   // name -> ok / failed / not run / the standing
};

// One package sent into the operator's package store (POST
// /v1/submissions/{id}/packages/{sha256}): the submission's packages check
// afterwards ("waiting" until every package is there, then "ok") and its
// state, which leaves failed_checks once the packages are all in.
struct PackageUpload {
    std::string sha256, fileName;   // <sha256>.<format>
    uint64_t size = 0;
    std::string packagesCheck, state;
    bool skipped = false;           // the service had the bytes already
};

class MarketClient {
public:
    MarketClient(MarketSettings settings, std::vector<uint8_t> seed, MarketCall call = UltraNetMarketCall());

    // The merchant's standing, strikes and terms, and its profile.
    Result<Standing> GetStanding() const;
    // PUT /v1/merchants/me: the profile the merchant edits itself; a key
    // given rotates it (signed with the old one, as every request here).
    Result<Standing> UpdateProfile(const ProfileChange& change) const;

    // ----- before the merchant id: a shop asked for -----
    // POST /v1/merchants/apply: the application signed with this backend's
    // key (the public half goes with it), no merchant id needed. The
    // receipt's id and secret are kept in the settings for Application().
    Result<ApplicationReceipt> Apply(const std::string& name, const std::string& contact, const std::string& email, const std::string& website,
                                     const std::string& description) const;
    // GET /v1/applications/{id}?secret=: public, no key needed.
    static Result<Application> ApplicationState(const std::string& url, const std::string& id, const std::string& secret, const MarketCall& call = UltraNetMarketCall());
    // A component of this backend's database, signed and sent as a
    // submission; `answers` names the submission a resubmission answers.
    Result<SubmissionReceipt> Submit(const UltraStore::Component& component, const std::string& answers = {}) const;
    // The file's bytes into the package store for a submission that names
    // their hash; the request is signed over the bytes like every other.
    Result<PackageUpload> UploadPackage(const std::string& submissionId, const std::string& sha256, const std::string& fileUtf8) const;
    // Every package of the component whose file is in the local package
    // store (packagesDirUtf8/<sha256>.<format>); one that is not there is
    // reported, not an error - its URL stays the merchant's. `each` is told
    // of every package as it goes (the window's lines).
    Result<std::vector<PackageUpload>> UploadPackages(const std::string& submissionId, const UltraStore::Component& component,
                                                      const std::string& packagesDirUtf8,
                                                      const std::function<void(const PackageUpload&)>& each = nullptr) const;
    // The merchant's submissions, every decision in the JSON.
    Result<UltraCanvas::JSONValue> Submissions() const;
    Result<void> Withdraw(const std::string& submissionId) const;
    // The notices addressed to the merchant, the unacknowledged first.
    Result<std::vector<Notice>> Notices() const;
    Result<std::string> Acknowledge(const std::string& noticeId) const;   // the time acknowledged
    // The terms in force (NotFound when none), and an acceptance.
    Result<Terms> CurrentTerms() const;
    Result<Standing> AcceptTerms(int version, const std::string& acceptedBy) const;
    // The reports about the merchant, its apps and its submissions, the
    // reporter removed by the service.
    Result<UltraCanvas::JSONValue> Reports() const;

    const MarketSettings& Settings() const { return settings_; }

private:
    Result<UltraCanvas::JSONValue> Ask(const std::string& method, const std::string& path, const std::string& jsonBody = {}) const;
    static Result<UltraCanvas::JSONValue> Answer(const MarketReply& reply);

    MarketSettings settings_;
    std::vector<uint8_t> seed_;
    MarketCall call_;
};

// The packages of a component as a submission names them: the file (the
// path under the packages server) or the URL, the SHA-256, the size, the
// format and the access (restricted: a paid app's without a trial), so the
// operator's package store can lay the file out under the right folder.
UltraCanvas::JSONValue SubmissionPackages(const UltraStore::Component& component);
// The local file behind a package: <packagesDir>/<sha256>.<format>, empty when it is not there.
std::string LocalPackageFile(const std::string& packagesDirUtf8, const UltraStore::Package& p);

// The seed of one of this backend's keys, opened with the passphrase, for
// the moment of signing: the key given, or the first active one when
// keyId is 0 (the setting market.key names one). The caller zeroes it.
Result<std::vector<uint8_t>> OpenKeySeed(const AdminDatabase& db, int64_t keyId, const std::string& passphrase);

} // namespace UltraStoreAdmin
