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
// Version: 0.1.1 - MarketCall speaks PUT as well (the operator's client shares it)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "AdminDatabase.h"
#include "UltraStoreTypes.h"

#include "DataFormats/UltraCanvasJSON.h"

#include <cstdint>
#include <functional>
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
    static MarketSettings From(const AdminDatabase& db);
    bool Complete() const { return !url.empty() && !merchantId.empty(); }
};

// A notice as the merchant sees it.
struct Notice {
    std::string id, kind, subject, text, clause, createdAt, deadline, acknowledgedAt;
    bool Acknowledged() const { return !acknowledgedAt.empty(); }
};

// The merchant's standing, from GET /v1/merchants/me.
struct Standing {
    std::string id, name, standing;   // active, paused, suspended
    int strikes = 0;
    int termsInForce = 0, termsAccepted = 0;
    bool TermsDue() const { return termsInForce > termsAccepted; }
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

class MarketClient {
public:
    MarketClient(MarketSettings settings, std::vector<uint8_t> seed, MarketCall call = UltraNetMarketCall());

    // The merchant's standing, strikes and terms.
    Result<Standing> GetStanding() const;
    // A component of this backend's database, signed and sent as a
    // submission; `answers` names the submission a resubmission answers.
    Result<SubmissionReceipt> Submit(const UltraStore::Component& component, const std::string& answers = {}) const;
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

    MarketSettings settings_;
    std::vector<uint8_t> seed_;
    MarketCall call_;
};

// The packages of a component as a submission names them: the file (the
// path under the packages server) or the URL, the SHA-256 and the size.
UltraCanvas::JSONValue SubmissionPackages(const UltraStore::Component& component);

// The seed of one of this backend's keys, opened with the passphrase, for
// the moment of signing: the key given, or the first active one when
// keyId is 0 (the setting market.key names one). The caller zeroes it.
Result<std::vector<uint8_t>> OpenKeySeed(const AdminDatabase& db, int64_t keyId, const std::string& passphrase);

} // namespace UltraStoreAdmin
