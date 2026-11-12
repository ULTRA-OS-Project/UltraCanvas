// Tests/UltraStore/test_purchase.cpp
// Buying through the payment service, against an HTTP call that answers
// from a map: the checkout, the order polled to paid, the licence verified
// with the key the catalog names (and refused when tampered or signed by
// another key), the signed download URL, the restricted file of a package,
// the registry's licences, the engine refusing a restricted package without
// a signer and fetching it through one.
// Version: 0.3.0 - subscriptions, revoked licences, SyncLicences
// Version: 0.2.0 - the terms, the licence file
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "fixtures.h"
#include "helpers.h"

#include "Purchase.h"
#include "Registry.h"
#include "StoreEngine.h"
#include "Verifier.h"

#include "UltraCanvasPathUtf8.h"

#include "DataFormats/UltraCanvasJSON.h"

#include <filesystem>
#include <map>

using namespace UltraStore;
using namespace ultrastore_test;

namespace {

// base64url without padding, as the service encodes.
std::string B64Url(const std::string& bytes) {
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    uint32_t buffer = 0;
    int bits = 0;
    for (unsigned char c : bytes) {
        buffer = (buffer << 8) | c;
        bits += 8;
        while (bits >= 6) { bits -= 6; out += alphabet[(buffer >> bits) & 63]; }
    }
    if (bits > 0) out += alphabet[(buffer << (6 - bits)) & 63];
    return out;
}

std::string MakeLicence(const std::vector<uint8_t>& seed, const std::string& app, const std::string& order,
                        const std::string& updates = "always", const std::string& until = "", const std::string& validUntil = "") {
    const std::string payload = R"({"app":")" + app + R"(","issued":"2026-10-27","licence":"lic-1","order":")" + order +
                                R"(","updates":")" + updates + (until.empty() ? "" : R"(","updates_until":")" + until) +
                                R"(","v":1)" + (validUntil.empty() ? "" : R"(,"valid_until":")" + validUntil + "\"") + "}";
    const auto sig = SignCatalog(payload, seed);
    return "ULIC1." + B64Url(payload) + "." + B64Url(std::string(sig.Value().begin(), sig.Value().end()));
}

struct FakeService {
    std::map<std::string, std::string> answers;   // "METHOD url" -> body
    std::vector<std::string> calls;
    int status = 200;
    HttpCall Call() {
        return [this](const std::string& method, const std::string& url, const std::string& body) {
            calls.push_back(method + " " + url + " " + body);
            HttpReply r;
            auto it = answers.find(method + " " + url);
            if (it == answers.end()) { r.status = 404; r.body = R"({"error":"no_such_order","error_description":"No such order"})"; return r; }
            r.status = status;
            r.body = it->second;
            return r;
        };
    }
};

} // namespace

TEST(checkout_poll_licence_and_download_through_the_service) {
    std::vector<uint8_t> seed, pk;
    REQUIRE(GenerateSigningKey(seed, pk));
    const std::string key = ToHex(pk);
    FakeService s;
    s.answers["POST https://pay.example/v1/checkout"] =
        R"({"order_id":"ord1","secret":"sec1","checkout_url":"https://pay.example/pay/ord1","app":"org.ultraos.ultravideo","app_name":"UltraVideo",)"
        R"("amount":"23.92","currency":"EUR","list_price":"29.90","code":"ANNA-20","code_applied":true,"status":"open"})";
    const auto order = StartCheckout("https://pay.example/", "org.ultraos.ultravideo", "anna-20", s.Call());
    REQUIRE(order);
    REQUIRE_EQ(order.Value().orderId, std::string("ord1"));
    REQUIRE_EQ(order.Value().amount, std::string("23.92"));
    REQUIRE(order.Value().codeApplied);
    REQUIRE(!order.Value().Paid());
    REQUIRE(s.calls[0].find(R"("code":"anna-20")") != std::string::npos);
    REQUIRE(!StartCheckout("", "org.ultraos.ultravideo", "", s.Call()));   // no service named

    // Open, then paid.
    s.answers["GET https://pay.example/v1/orders/ord1?secret=sec1"] = R"({"order_id":"ord1","app":"org.ultraos.ultravideo","status":"open","amount":"23.92","currency":"EUR"})";
    REQUIRE(!OrderStatus("https://pay.example", "ord1", "sec1", s.Call()).Value().Over());
    const std::string token = MakeLicence(seed, "org.ultraos.ultravideo", "ord1");
    s.answers["GET https://pay.example/v1/orders/ord1?secret=sec1"] =
        R"({"order_id":"ord1","app":"org.ultraos.ultravideo","status":"paid","amount":"23.92","currency":"EUR","paid_at":"2026-10-27T12:00:00Z","licence_id":"lic-1","receipt_no":7,"licence":")" + token + R"("})";
    const auto paid = OrderStatus("https://pay.example", "ord1", "sec1", s.Call());
    REQUIRE(paid);
    REQUIRE(paid.Value().Paid());
    REQUIRE_EQ(paid.Value().receiptNo, 7);
    REQUIRE_EQ(paid.Value().secret, std::string("sec1"));
    REQUIRE(OrderStatus("https://pay.example", "ord1", "wrong", s.Call()).error.code == ErrorCode::NotFound);

    // The licence: this key's, not another's, not tampered.
    const auto licence = VerifyLicence(paid.Value().licence, key);
    REQUIRE(licence);
    REQUIRE_EQ(licence.Value().app, std::string("org.ultraos.ultravideo"));
    REQUIRE_EQ(licence.Value().orderId, std::string("ord1"));
    REQUIRE_EQ(licence.Value().licenceId, std::string("lic-1"));
    std::vector<uint8_t> seed2, pk2;
    REQUIRE(GenerateSigningKey(seed2, pk2));
    REQUIRE(VerifyLicence(token, ToHex(pk2)).error.code == ErrorCode::SignatureInvalid);
    REQUIRE(VerifyLicence(MakeLicence(seed, "org.ultraos.other", "ord1"), key).Value().app == "org.ultraos.other");
    std::string tampered = token;
    tampered[10] = tampered[10] == 'A' ? 'B' : 'A';
    REQUIRE(!VerifyLicence(tampered, key));
    REQUIRE(VerifyLicence("hello", key).error.code == ErrorCode::ParseError);
    REQUIRE(VerifyLicence(token, "nothex").error.code == ErrorCode::InvalidArgument);

    // The record the registry keeps.
    const LicenceRecord record = LicenceRecordOf(paid.Value(), licence.Value());
    REQUIRE_EQ(record.app, std::string("org.ultraos.ultravideo"));
    REQUIRE_EQ(record.orderSecret, std::string("sec1"));
    REQUIRE_EQ(record.receiptNo, 7);
    REQUIRE_EQ(record.token, token);

    // The download URL.
    REQUIRE_EQ(RestrictedFileOf("https://store.example/v1/restricted/abc.zip"), std::string("restricted/abc.zip"));
    REQUIRE_EQ(RestrictedFileOf("https://store.example/v1/restricted/abc.zip?md5=x"), std::string("restricted/abc.zip"));
    REQUIRE(RestrictedFileOf("https://store.example/v1/packages/abc.zip").empty());
    s.answers["POST https://pay.example/v1/orders/ord1/download?secret=sec1&file=restricted%2Fabc.zip"] = R"({"url":"https://store.example/v1/restricted/abc.zip?md5=m&expires=1","expires":1})";
    const auto url = SignedDownloadUrl("https://pay.example", "ord1", "sec1", "restricted/abc.zip", s.Call());
    REQUIRE(url);
    REQUIRE_EQ(url.Value(), std::string("https://store.example/v1/restricted/abc.zip?md5=m&expires=1"));
    s.status = 503;
    s.answers["POST https://pay.example/v1/checkout"] = R"({"error":"not_configured","error_description":"The payment service is not configured: X"})";
    const auto down = StartCheckout("https://pay.example", "org.ultraos.ultravideo", "", s.Call());
    REQUIRE(!down);
    REQUIRE(down.error.message.find("not configured") != std::string::npos);
}

TEST(the_registry_keeps_one_licence_per_app) {
    const std::string dir = TempRoot("licences");
    SqliteRegistry registry;
    REQUIRE(registry.Open(dir + "/registry.sqlite"));
    REQUIRE(registry.GetLicence("org.x.a").error.code == ErrorCode::NotFound);
    LicenceRecord l;
    l.app = "org.x.a";
    l.licenceId = "lic-1";
    l.orderId = "ord1";
    l.orderSecret = "sec";
    l.token = "ULIC1.x.y";
    l.issuedAt = "2026-10-27";
    l.amount = "23.92";
    l.currency = "EUR";
    l.receiptNo = 7;
    REQUIRE(registry.PutLicence(l));
    REQUIRE_EQ(registry.GetLicence("org.x.a").Value().orderSecret, std::string("sec"));
    REQUIRE_EQ(registry.GetLicence("org.x.a").Value().receiptNo, 7);
    l.receiptNo = 8;
    REQUIRE(registry.PutLicence(l));   // replaces
    REQUIRE_EQ(registry.Licences().Value().size(), size_t(1));
    REQUIRE_EQ(registry.Licences().Value()[0].receiptNo, 8);
    LicenceRecord bad;
    bad.app = "org.x.b";
    REQUIRE(!registry.PutLicence(bad));   // no token
    REQUIRE(registry.RemoveLicence("org.x.a"));
    REQUIRE(registry.Licences().Value().empty());
    // Opened again: schema 2 is read, the table is there.
    registry.Close();
    REQUIRE(registry.Open(dir + "/registry.sqlite"));
    REQUIRE(registry.Licences());
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST(what_the_purchase_includes_decides_which_releases_are_covered) {
    Licence always;
    always.updates = "always";
    REQUIRE(always.Covers("2099-01-01"));
    REQUIRE_EQ(always.UpdateTerms(), std::string("updates included"));
    Licence never;
    never.updates = "never";
    never.issued = "2026-10-27";
    REQUIRE(never.Covers("2026-10-27"));
    REQUIRE(never.Covers(""));             // an undated release is in
    REQUIRE(!never.Covers("2026-10-28"));
    REQUIRE_EQ(never.UpdateTerms(), std::string("the bought version only (2026-10-27)"));
    Licence until;
    until.updates = "until";
    until.updatesUntil = "2027-12-31";
    REQUIRE(until.Covers("2027-12-31"));
    REQUIRE(!until.Covers("2028-01-01"));
    REQUIRE_EQ(until.UpdateTerms(), std::string("updates until 2027-12-31"));
    Licence unknown;
    unknown.updates = "something-newer";
    REQUIRE(unknown.Covers("2099-01-01"));   // not this store's place to refuse
    // The payload parsed without a key carries the same.
    std::vector<uint8_t> seed, pk;
    REQUIRE(GenerateSigningKey(seed, pk));
    const std::string keyHex = ToHex(pk);
    const auto parsed = ParseLicence(MakeLicence(seed, "org.x.app", "o-1", "until", "2027-12-31"));
    REQUIRE(parsed);
    REQUIRE_EQ(parsed.Value().updates, std::string("until"));
    REQUIRE_EQ(parsed.Value().updatesUntil, std::string("2027-12-31"));
    REQUIRE(!ParseLicence("not a licence"));
}

TEST(the_licence_file_is_found_from_the_app_and_verified_with_the_key) {
    std::vector<uint8_t> seed, pk;
    REQUIRE(GenerateSigningKey(seed, pk));
    const std::string keyHex = ToHex(pk);
    const std::string root = TempRoot("licence-file");
    const std::string token = MakeLicence(seed, "org.x.app", "o-1", "never");
    const std::string file = LicenceFilePath(root, "org.x.app");
    REQUIRE(file.find("org.x.app") != std::string::npos && file.size() > std::string(kLicenceFileName).size() &&
            file.compare(file.size() - std::string(kLicenceFileName).size(), std::string::npos, kLicenceFileName) == 0);
    REQUIRE(WriteLicenceFile(file, token));
    // The app's executable sits in apps/<id>/<version>/bin/: three folders below the file.
    const std::string exe = UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(root) / "apps" / "org.x.app" / "1.0" / "bin" / "app");
    std::error_code ec;
    std::filesystem::create_directories(UltraCanvas::PathFromUtf8(exe).parent_path(), ec);
    REQUIRE_EQ(FindLicenceFile(exe), file);
    REQUIRE_EQ(FindLicenceFile(root), std::string());   // nothing above apps/
    const auto read = ReadLicenceFile(FindLicenceFile(exe), keyHex);
    REQUIRE(read);
    REQUIRE_EQ(read.Value().app, std::string("org.x.app"));
    REQUIRE_EQ(read.Value().updates, std::string("never"));
    std::vector<uint8_t> otherSeed, otherPk;
    REQUIRE(GenerateSigningKey(otherSeed, otherPk));
    REQUIRE(!ReadLicenceFile(file, ToHex(otherPk)));
    REQUIRE(ReadLicenceFile("", keyHex).error.code == ErrorCode::NotFound);
    std::filesystem::remove_all(UltraCanvas::PathFromUtf8(root), ec);
}

TEST(a_subscription_licence_is_valid_until_its_day) {
    Licence forever;
    REQUIRE(forever.ValidOn("2099-12-31"));
    Licence sub;
    sub.validUntil = "2026-12-18";
    REQUIRE(sub.ValidOn("2026-12-18"));
    REQUIRE(sub.ValidOn(""));
    REQUIRE(!sub.ValidOn("2026-12-19"));
    std::vector<uint8_t> seed, pk;
    REQUIRE(GenerateSigningKey(seed, pk));
    const auto parsed = VerifyLicence(MakeLicence(seed, "org.x.app", "o-1", "always", "", "2026-12-18"), ToHex(pk));
    REQUIRE(parsed);
    REQUIRE_EQ(parsed.Value().validUntil, std::string("2026-12-18"));
    REQUIRE(parsed.Value().Covers("2099-01-01"));   // the updates are a separate term
    // The order carries the subscription and the revocation.
    FakeService s;
    s.answers["GET https://pay.example/v1/orders/sub1?secret=sec"] =
        R"({"order_id":"sub1","app":"org.x.app","status":"paid","amount":"4.90","currency":"EUR","cycle":"month","paid_until":"2026-12-11",)"
        R"("valid_until":"2026-12-18","subscription_status":"active","licence_id":"lic-1","licence":"ULIC1.a.b"})";
    const auto sub1 = OrderStatus("https://pay.example", "sub1", "sec", s.Call());
    REQUIRE(sub1);
    REQUIRE(sub1.Value().Subscription());
    REQUIRE_EQ(sub1.Value().paidUntil, std::string("2026-12-11"));
    REQUIRE_EQ(sub1.Value().validUntil, std::string("2026-12-18"));
    REQUIRE_EQ(sub1.Value().subscriptionStatus, std::string("active"));
    REQUIRE(!sub1.Value().revoked);
    s.answers["GET https://pay.example/v1/orders/gone?secret=sec"] =
        R"({"order_id":"gone","app":"org.x.b","status":"refunded","amount":"29.90","currency":"EUR","revoked":true,"revoked_reason":"refund","refunded":"29.90"})";
    const auto gone = OrderStatus("https://pay.example", "gone", "sec", s.Call());
    REQUIRE(gone);
    REQUIRE(gone.Value().revoked && gone.Value().Over() && !gone.Value().Paid());
    REQUIRE_EQ(gone.Value().revokedReason, std::string("refund"));
    // Cancelling: a POST to the subscription, the order back with its new status.
    s.answers["POST https://pay.example/v1/orders/sub1/subscription/cancel?secret=sec"] =
        R"({"order_id":"sub1","app":"org.x.app","status":"paid","cycle":"month","paid_until":"2026-12-11","valid_until":"2026-12-18","subscription_status":"cancelled"})";
    const auto cancelled = CancelSubscription("https://pay.example/", "sub1", "sec", s.Call());
    REQUIRE(cancelled);
    REQUIRE_EQ(cancelled.Value().subscriptionStatus, std::string("cancelled"));
    REQUIRE_EQ(cancelled.Value().secret, std::string("sec"));
    REQUIRE(s.calls.back().rfind("POST https://pay.example/v1/orders/sub1/subscription/cancel?secret=sec", 0) == 0);
}

TEST(the_licences_are_synced_with_the_service) {
    std::vector<uint8_t> seed, pk;
    REQUIRE(GenerateSigningKey(seed, pk));
    const std::string key = ToHex(pk);
    const std::string root = TempRoot("licence-sync");
    StoreEngine engine(root);
    REQUIRE(engine.Open());
    auto put = [&](const std::string& app, const std::string& order, const std::string& token) {
        LicenceRecord l;
        l.app = app;
        l.licenceId = "lic-1";
        l.orderId = order;
        l.orderSecret = "sec";
        l.token = token;
        l.issuedAt = "2026-10-27";
        l.amount = "4.90";
        l.currency = "EUR";
        l.receiptNo = 1;
        REQUIRE(engine.Registry().PutLicence(l));
    };
    const std::string stays = MakeLicence(seed, "org.x.stays", "o-stays");
    const std::string first = MakeLicence(seed, "org.x.renews", "o-renews", "always", "", "2026-12-18");
    const std::string second = MakeLicence(seed, "org.x.renews", "o-renews", "always", "", "2027-01-18");
    put("org.x.stays", "o-stays", stays);
    put("org.x.renews", "o-renews", first);
    put("org.x.refunded", "o-refunded", MakeLicence(seed, "org.x.refunded", "o-refunded"));
    put("org.x.offline", "o-offline", MakeLicence(seed, "org.x.offline", "o-offline"));
    put("org.x.forged", "o-forged", MakeLicence(seed, "org.x.forged", "o-forged"));
    // The renewed and the refunded apps have licence files; the files follow the registry.
    REQUIRE(engine.WriteLicenceFile("org.x.renews"));
    REQUIRE(engine.WriteLicenceFile("org.x.refunded"));
    std::vector<uint8_t> otherSeed, otherPk;
    REQUIRE(GenerateSigningKey(otherSeed, otherPk));
    FakeService s;
    s.answers["GET https://pay.example/v1/orders/o-stays?secret=sec"] = R"({"order_id":"o-stays","status":"paid","licence_id":"lic-1","licence":")" + stays + R"("})";
    s.answers["GET https://pay.example/v1/orders/o-renews?secret=sec"] =
        R"({"order_id":"o-renews","status":"paid","cycle":"month","valid_until":"2027-01-18","licence_id":"lic-1","licence":")" + second + R"("})";
    s.answers["GET https://pay.example/v1/orders/o-refunded?secret=sec"] = R"({"order_id":"o-refunded","status":"refunded","revoked":true,"revoked_reason":"refund"})";
    s.answers["GET https://pay.example/v1/orders/o-forged?secret=sec"] =
        R"({"order_id":"o-forged","status":"paid","licence_id":"lic-1","licence":")" + MakeLicence(otherSeed, "org.x.forged", "o-forged", "always", "", "2099-01-01") + R"("})";
    // o-offline: no answer (the fake says 404 for what it does not know - a NotFound the sync reports and keeps the record for)
    REQUIRE(engine.SyncLicences("", key, s.Call()).error.code == ErrorCode::NotAvailable);
    const auto sync = engine.SyncLicences("https://pay.example", key, s.Call());
    REQUIRE(sync);
    REQUIRE_EQ(sync.Value().renewed.size(), size_t(1));
    REQUIRE_EQ(sync.Value().renewed[0], std::string("org.x.renews"));
    REQUIRE_EQ(sync.Value().revoked.size(), size_t(1));
    REQUIRE_EQ(sync.Value().revoked[0], std::string("org.x.refunded"));
    REQUIRE_EQ(sync.Value().failed.size(), size_t(2));   // offline (no answer), forged (another key's token)
    REQUIRE(sync.Value().Changed());
    // The registry: the renewed token in place, the refunded record gone, the others as they were.
    REQUIRE_EQ(engine.Registry().GetLicence("org.x.renews").Value().token, second);
    REQUIRE_EQ(engine.Registry().GetLicence("org.x.stays").Value().token, stays);
    REQUIRE(engine.Registry().GetLicence("org.x.refunded").error.code == ErrorCode::NotFound);
    REQUIRE(engine.Registry().GetLicence("org.x.offline"));
    REQUIRE(engine.Registry().GetLicence("org.x.forged"));
    // The files: the renewed one carries the new token, the refunded one is gone.
    REQUIRE_EQ(ReadLicenceFile(LicenceFilePath(root, "org.x.renews"), key).Value().validUntil, std::string("2027-01-18"));
    std::error_code ec;
    REQUIRE(!std::filesystem::exists(UltraCanvas::PathFromUtf8(LicenceFilePath(root, "org.x.refunded")), ec));
    // Again: nothing changes.
    const auto again = engine.SyncLicences("https://pay.example", key, s.Call());
    REQUIRE(again);
    REQUIRE(!again.Value().Changed());
    std::filesystem::remove_all(UltraCanvas::PathFromUtf8(root), ec);
}
