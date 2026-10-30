// Apps/UltraStore/core/Purchase.h
// Buying an app through the payment service (Server/UltraStorePay, the
// store's README, *Payment*): the checkout asked for, the order polled until
// Skrill's report paid it, the licence verified with the key the catalog
// names, and the signed URL for a paid app's restricted package. The HTTP
// call is a function the caller hands in, so the tests use a map and nothing
// here touches the network in a test; the default is UltraNet.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "Catalog.h"
#include "Registry.h"
#include "UltraStoreTypes.h"

#include <functional>
#include <string>

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
    std::string status;       // open, pending, paid, cancelled, failed, chargeback
    std::string paidAt, licenceId, licence;
    int receiptNo = 0;
    bool Paid() const { return status == "paid"; }
    bool Over() const { return status == "paid" || status == "cancelled" || status == "failed" || status == "chargeback"; }
};

// POST /v1/checkout: the order, with the checkout URL to open in the browser.
Result<Order> StartCheckout(const std::string& paymentUrl, const std::string& appId, const std::string& code, const HttpCall& http);
// GET /v1/orders/{id}?secret=: the order as it stands.
Result<Order> OrderStatus(const std::string& paymentUrl, const std::string& orderId, const std::string& secret, const HttpCall& http);
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
    // Whether a release dated so is in the purchase: always; never - no
    // later than the day the licence was issued; until - no later than
    // that day. An undated release is in.
    bool Covers(const std::string& releaseDate) const;
    // "updates included", "the bought version only (2026-10-27)", "updates until 2027-12-31"
    std::string UpdateTerms() const;
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

} // namespace UltraStore
