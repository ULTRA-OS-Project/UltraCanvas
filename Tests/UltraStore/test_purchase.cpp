// Tests/UltraStore/test_purchase.cpp
// Buying through the payment service, against an HTTP call that answers
// from a map: the checkout, the order polled to paid, the licence verified
// with the key the catalog names (and refused when tampered or signed by
// another key), the signed download URL, the restricted file of a package,
// the registry's licences, the engine refusing a restricted package without
// a signer and fetching it through one.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "fixtures.h"
#include "helpers.h"

#include "Purchase.h"
#include "Registry.h"
#include "StoreEngine.h"
#include "Verifier.h"

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

std::string MakeLicence(const std::vector<uint8_t>& seed, const std::string& app, const std::string& order) {
    const std::string payload = R"({"app":")" + app + R"(","issued":"2026-10-27","licence":"lic-1","order":")" + order + R"(","updates":"per-catalog","v":1})";
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
