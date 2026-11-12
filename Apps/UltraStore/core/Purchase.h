// Apps/UltraStore/core/Purchase.h
// Buying an app through the payment service (Server/UltraStorePay, the
// store's README, *Payment*): the checkout asked for, the order polled until
// Skrill's report paid it, the licence verified with the key the catalog
// names, and the signed URL for a paid app's restricted package. The HTTP
// call is a function the caller hands in, so the tests use a map and nothing
// here touches the network in a test; the default is UltraNet.
// Version: 0.2.0 - subscriptions (the order's cycle and days, the licence's valid_until, CancelSubscription),
//                  a licence taken back by a refund or a chargeback, SyncLicences
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "Catalog.h"
#include "Registry.h"
#include "UltraStoreTypes.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace UltraStore {

// What an HTTP call gives back: the status (0 when nothing came) and the body.
struct HttpReply {
    int status = 0;
    std::string body;
    std::string error;    // empty when the server answered, whatever it said
};
// method "GET" or "POST"; a POST sends jsonBody as application/json.
using HttpCall = std::function<HttpReply(const std::string& method, const std::string& url, const std::string& jsonBody)>;
HttpCall UltraNetHttpCall();   // HTTPS, redirects to https:// only

// An order of the payment service, as POST /v1/checkout and
// GET /v1/orders/{id} describe it.
struct Order {
    std::string orderId, secret, checkoutUrl;
    std::string app, appName;
    std::string amount, currency, listPrice;
    std::string code;
    bool codeApplied = false;
    std::string status;       // open, pending, paid, cancelled, failed, chargeback, refunded
    std::string paidAt, licenceId, licence;
    int receiptNo = 0;
    // A subscription: "month" or "year", the day the paid period ends, the
    // day the licence is valid until (the period plus the service's grace
    // days), and whether it is active, failing (a renewal did not go
    // through) or cancelled (it ends with the paid period).
    std::string cycle, paidUntil, validUntil, subscriptionStatus;
    // The licence was taken back by a refund or a chargeback (revokedReason).
    bool revoked = false;
    std::string revokedReason;
    bool Paid() const { return status == "paid"; }
    bool Subscription() const { return !cycle.empty(); }
    bool Over() const { return status == "paid" || status == "cancelled" || status == "failed" || status == "chargeback" || status == "refunded"; }
};

// POST /v1/checkout: the order, with the checkout URL to open in the browser.
Result<Order> StartCheckout(const std::string& paymentUrl, const std::string& appId, const std::string& code, const HttpCall& http);
// GET /v1/orders/{id}?secret=: the order as it stands.
Result<Order> OrderStatus(const std::string& paymentUrl, const std::string& orderId, const std::string& secret, const HttpCall& http);
// POST /v1/orders/{id}/subscription/cancel: the buyer ends a subscription;
// Skrill charges no more and the licence lasts until the paid period ends.
// The order as it stands afterwards.
Result<Order> CancelSubscription(const std::string& paymentUrl, const std::string& orderId, const std::string& secret, const HttpCall& http);
// POST /v1/orders/{id}/download: a URL for one of the app's restricted
// packages ("restricted/<sha256>.<format>"), good for a few minutes.
Result<std::string> SignedDownloadUrl(const std::string& paymentUrl, const std::string& orderId, const std::string& secret,
                                      const std::string& file, const HttpCall& http);
// "restricted/<sha256>.<format>" from a package's URL, or "" when the
// package is not restricted.
std::string RestrictedFileOf(const std::string& packageUrl);

// The engine's URL signer for a store with a licence registry: a
// restricted package of an app bought here is fetched through the URL the
// payment service signs for its order; an app not bought gives "" and the
// engine refuses the install. Runs on the engine's thread.
std::function<std::string(const Package&, const Component&)>
LicensedUrlSigner(SqliteRegistry& registry, std::string paymentUrl, HttpCall http = UltraNetHttpCall());

// What a licence says, once its signature checked out: who bought what,
// and what the purchase includes - `updates` is "always", "never" (the
// bought version only) or "until" a day (`updatesUntil`), the catalog's
// update period when the order was paid.
struct Licence {
    std::string licenceId, app, orderId, issued, updates, updatesUntil;
    // A subscription's licence: the day it is valid until; empty for a
    // purchase that lasts.
    std::string validUntil;
    // Whether a release dated so is in the purchase: always; never - no
    // later than the day the licence was issued; until - no later than
    // that day. An undated release is in.
    bool Covers(const std::string& releaseDate) const;
    // "updates included", "the bought version only (2026-10-27)", "updates until 2027-12-31"
    std::string UpdateTerms() const;
    // Whether the licence is good on that day: always for a purchase that
    // lasts, up to and including validUntil for a subscription's. An app
    // checks this with today's date after ReadLicenceFile.
    bool ValidOn(const std::string& day) const;
};
// "ULIC1.<base64url payload>.<base64url signature>" against the hex Ed25519
// key the catalog names (licence_key); SignatureInvalid when it is not that
// key's, ParseError when it is not a licence at all.
Result<Licence> VerifyLicence(const std::string& token, const std::string& publicKeyHex);
// The payload alone, unverified: for what the store already verified when
// it kept the token (the registry's record).
Result<Licence> ParseLicence(const std::string& token);

// ----- the licence file -----
// The app gets its licence as a file beside its versions, <root>/apps/<id>/
// licence.ulic, written when the app is installed or bought and removed with
// the app. An app finds it from its own executable (FindLicenceFile walks
// up from the executable's folder) and verifies it with the licence key it
// compiles in:
//   const std::string file = FindLicenceFile(PathToUtf8(executable));
//   const auto licence = ReadLicenceFile(file, kLicenceKeyHex);
//   if (licence && licence.Value().app == "org.example.app") ...unlocked...
constexpr const char* kLicenceFileName = "licence.ulic";
std::string LicenceFilePath(const std::string& storeRootUtf8, const std::string& appId);
Result<void> WriteLicenceFile(const std::string& path, const std::string& token);
// The nearest licence.ulic in the folder of the path given or up to three
// folders above it, or "".
std::string FindLicenceFile(const std::string& pathUtf8);
// The file's token verified against the key: NotFound when there is no file.
Result<Licence> ReadLicenceFile(const std::string& pathUtf8, const std::string& publicKeyHex);

// The record the registry keeps of a paid order.
LicenceRecord LicenceRecordOf(const Order& order, const Licence& licence);

// ----- keeping the licences current -----
// What SyncLicences found: the apps whose licence the service re-issued (a
// subscription renewed), the apps whose licence it took back (a refund, a
// chargeback - the record is gone from the registry), and the apps it
// could not ask about, with why (their records stay).
struct LicenceSync {
    std::vector<std::string> renewed, revoked;
    std::vector<std::pair<std::string, Error>> failed;
    bool Changed() const { return !renewed.empty() || !revoked.empty(); }
};
// Asks the service about every licence in the registry (GET /v1/orders/
// {id} with the order's secret): a revoked one is removed, a re-issued
// token verified against the key (when one is given) and kept in place of
// the old. The engine's SyncLicences does the same and the licence files.
Result<LicenceSync> SyncLicences(SqliteRegistry& registry, const std::string& paymentUrl, const std::string& licenceKeyHex, const HttpCall& http);

} // namespace UltraStore
