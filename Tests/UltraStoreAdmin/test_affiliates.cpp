// Tests/UltraStoreAdmin/test_affiliates.cpp
// Affiliates and codes: the tables, the two-tier credit and no third, the
// report, the links, a QR code, the landing page, and what the catalog
// carries.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "admin_helpers.h"

#include "Affiliates.h"
#include "CatalogPublisher.h"
#include "KeyVault.h"

#include "UltraCrypt/UltraCryptCore.h"

#include <filesystem>

using namespace UltraStoreAdmin;
using namespace admin_test;

namespace {

Affiliate Aff(const char* id, const char* name, int share, int recruitShare, const char* recruitedBy = "") {
    Affiliate a;
    a.id = id;
    a.name = name;
    a.sharePercent = share;
    a.recruitSharePercent = recruitShare;
    a.recruitedBy = recruitedBy;
    return a;
}

CodeRecord Code(const char* code, UltraStore::CodeKind kind, const char* affiliate, int percent = 0) {
    CodeRecord r;
    r.code.code = code;
    r.code.kind = kind;
    r.code.affiliate = affiliate;
    r.code.discountPercent = percent;
    return r;
}

} // namespace

TEST(affiliates_and_codes_round_trip_with_their_rules) {
    const std::string dir = TempDir("affiliates");
    AdminDatabase db;
    REQUIRE(db.Open(Join(dir, "admin.sqlite")).Ok());
    REQUIRE(db.PutAffiliate(Aff("aff-super", "Super", 10, 5)).Ok());
    REQUIRE(db.PutAffiliate(Aff("aff-anna", "Anna", 20, 0, "aff-super")).Ok());
    REQUIRE(!db.PutAffiliate(Aff("aff-x", "X", 20, 0, "nobody")).Ok());        // the recruiter must exist
    REQUIRE(!db.PutAffiliate(Aff("aff-x", "X", 120, 0)).Ok());                 // a share is a percentage
    REQUIRE(!db.PutAffiliate(Aff("aff-x", "X", 20, 0, "aff-x")).Ok());         // not by itself
    REQUIRE(!db.PutAffiliate(Aff("aff-super", "Super", 10, 5, "aff-anna")).Ok());   // no loop: Anna was recruited by Super
    REQUIRE(!db.PutAffiliate(Aff("bad id!", "X", 1, 0)).Ok());
    const auto all = db.Affiliates();
    REQUIRE(all.Ok());
    REQUIRE_EQ(all.Value().size(), size_t(2));
    REQUIRE_EQ(db.GetAffiliate("aff-anna").Value().recruitedBy, std::string("aff-super"));
    REQUIRE(!db.GetAffiliate("aff-anna").Value().createdAt.empty());

    REQUIRE(db.PutCode(Code("anna-20", UltraStore::CodeKind::Affiliate, "aff-anna", 20)).Ok());
    REQUIRE(!db.PutCode(Code("ghost", UltraStore::CodeKind::Affiliate, "aff-nobody")).Ok());   // the affiliate must exist
    REQUIRE(!db.PutCode(Code("", UltraStore::CodeKind::Discount, "")).Ok());
    REQUIRE(!db.PutCode(Code("a*b", UltraStore::CodeKind::Discount, "")).Ok());
    REQUIRE(db.PutCode(Code(" a b ", UltraStore::CodeKind::Discount, "", 1)).Ok());   // spaces are dropped: AB
    REQUIRE(db.RemoveCode("AB").Ok());
    CodeRecord both = Code("both", UltraStore::CodeKind::Discount, "", 10);
    both.code.discountAmount = "5";
    REQUIRE(!db.PutCode(both).Ok());                                            // a percentage or an amount
    CodeRecord five = Code("five", UltraStore::CodeKind::Discount, "");
    five.code.discountAmount = "5.00";
    five.code.currency = "EUR";
    five.code.apps = {"org.ultraos.hello"};
    five.code.validUntil = "2027-12-31";
    REQUIRE(db.PutCode(five).Ok());
    const auto got = db.GetCode("  Anna-20 ");                                  // normalized on the way in and out
    REQUIRE(got.Ok());
    REQUIRE_EQ(got.Value().code.code, std::string("ANNA-20"));
    REQUIRE_EQ(got.Value().code.affiliate, std::string("aff-anna"));
    REQUIRE_EQ(db.GetCode("five").Value().code.apps.size(), size_t(1));
    REQUIRE_EQ(db.Codes().Value().size(), size_t(2));
    // An affiliate with a code, or with a recruit, stays until that is undone.
    REQUIRE(!db.RemoveAffiliate("aff-anna").Ok());
    REQUIRE(!db.RemoveAffiliate("aff-super").Ok());
    REQUIRE(db.RemoveCode("ANNA-20").Ok());
    REQUIRE(!db.RemoveCode("ANNA-20").Ok());
    REQUIRE(db.RemoveAffiliate("aff-anna").Ok());
    REQUIRE(db.RemoveAffiliate("aff-super").Ok());
    RemoveDir(dir);
}

TEST(a_sale_credits_the_affiliate_and_the_recruiter_one_level_up_and_nobody_above) {
    const std::string dir = TempDir("credits");
    AdminDatabase db;
    REQUIRE(db.Open(Join(dir, "admin.sqlite")).Ok());
    // Top recruited Super, Super recruited Anna. Anna's code sells.
    REQUIRE(db.PutAffiliate(Aff("aff-top", "Top", 10, 8)).Ok());
    REQUIRE(db.PutAffiliate(Aff("aff-super", "Super", 10, 5, "aff-top")).Ok());
    REQUIRE(db.PutAffiliate(Aff("aff-anna", "Anna", 20, 0, "aff-super")).Ok());
    REQUIRE(db.PutCode(Code("anna-20", UltraStore::CodeKind::Affiliate, "aff-anna", 20)).Ok());
    REQUIRE(db.PutCode(Code("super", UltraStore::CodeKind::Affiliate, "aff-super")).Ok());
    REQUIRE(db.PutCode(Code("tenoff", UltraStore::CodeKind::Discount, "", 10)).Ok());

    REQUIRE_EQ(PercentOf("29.90", 20), std::string("5.98"));
    REQUIRE_EQ(PercentOf("0.01", 20), std::string("0.00"));   // 0.002, rounded half up to the cent
    REQUIRE_EQ(PercentOf("0.03", 20), std::string("0.01"));
    REQUIRE_EQ(AddDecimals("12.30", "0.7"), std::string("13.00"));
    REQUIRE_EQ(AddDecimals("", "5"), std::string("5.00"));

    const auto sale = RecordSale(db, "anna-20", "org.ultraos.hello", "23.92", "EUR", "2026-10-27T12:00:00Z");
    REQUIRE(sale.Ok());
    REQUIRE_EQ(sale.Value().affiliate, std::string("aff-anna"));
    REQUIRE_EQ(sale.Value().affiliateCredit, std::string("4.78"));    // 20% of 23.92
    REQUIRE_EQ(sale.Value().recruiter, std::string("aff-super"));
    REQUIRE_EQ(sale.Value().recruiterCredit, std::string("1.20"));    // Super's recruit share, 5%
    // Top recruited Super, but Anna's sale is two levels below Top: nothing.
    const auto report1 = AffiliateReport(db);
    REQUIRE(report1.Ok());
    for (const AffiliateTotals& t : report1.Value()) {
        if (t.affiliate.id == "aff-top") { REQUIRE_EQ(t.ownSales, size_t(0)); REQUIRE_EQ(t.recruitSales, size_t(0)); }
        if (t.affiliate.id == "aff-super") { REQUIRE_EQ(t.ownSales, size_t(0)); REQUIRE_EQ(t.recruitSales, size_t(1)); REQUIRE_EQ(t.recruitCredit[0].second, std::string("1.20")); }
        if (t.affiliate.id == "aff-anna") { REQUIRE_EQ(t.ownSales, size_t(1)); REQUIRE_EQ(t.ownCredit[0].second, std::string("4.78")); REQUIRE(t.recruitCredit.empty()); }
    }
    // Super's own code: Super earns their share, Top earns the recruit share.
    const auto sale2 = RecordSale(db, "SUPER", "org.ultraos.hello", "100", "EUR");
    REQUIRE(sale2.Ok());
    REQUIRE_EQ(sale2.Value().affiliateCredit, std::string("10.00"));
    REQUIRE_EQ(sale2.Value().recruiter, std::string("aff-top"));
    REQUIRE_EQ(sale2.Value().recruiterCredit, std::string("8.00"));
    REQUIRE(!sale2.Value().at.empty());
    // A discount code credits nobody; an unknown code neither; both are kept as sales.
    REQUIRE(RecordSale(db, "tenoff", "org.ultraos.hello", "26.91", "EUR").Value().affiliate.empty());
    REQUIRE(RecordSale(db, "", "org.ultraos.hello", "29.90", "EUR").Value().affiliate.empty());
    REQUIRE(RecordSale(db, "nope", "org.ultraos.hello", "29.90", "EUR").Value().affiliate.empty());
    REQUIRE(!RecordSale(db, "anna-20", "org.ultraos.hello", "29,90", "EUR").Ok());
    REQUIRE(!RecordSale(db, "anna-20", "org.ultraos.hello", "29.90", "euro").Ok());
    REQUIRE_EQ(db.Sales().Value().size(), size_t(5));
    // A disabled affiliate earns nothing, and neither does their recruiter through them.
    Affiliate anna = db.GetAffiliate("aff-anna").Value();
    anna.enabled = false;
    REQUIRE(db.PutAffiliate(anna).Ok());
    REQUIRE(RecordSale(db, "anna-20", "org.ultraos.hello", "23.92", "EUR").Value().affiliate.empty());
    const auto report2 = AffiliateReport(db);
    for (const AffiliateTotals& t : report2.Value()) {
        if (t.affiliate.id == "aff-super") { REQUIRE_EQ(t.recruitSales, size_t(1)); REQUIRE_EQ(t.ownCredit[0].second, std::string("10.00")); }
        if (t.affiliate.id == "aff-top") { REQUIRE_EQ(t.recruitSales, size_t(1)); REQUIRE_EQ(t.recruitCredit[0].second, std::string("8.00")); }
    }
    RemoveDir(dir);
}

TEST(links_qr_codes_and_the_landing_page) {
    REQUIRE_EQ(CodeLink("https://store.example/v1/", "anna-20"), std::string("https://store.example/v1/go/ANNA-20"));
    REQUIRE_EQ(JoinLink("https://store.example/v1", "aff-anna"), std::string("https://store.example/v1/join/aff-anna"));
    REQUIRE_EQ(StoreLink(" anna-20 "), std::string("ultrastore://code/ANNA-20"));
    const std::string dir = TempDir("qr");
    const std::string svg = Join(dir, "anna.svg");
    REQUIRE(WriteQrCodeSvg(CodeLink("https://store.example/v1", "anna-20"), svg).Ok());
    const std::string text = ReadFile(svg);
    REQUIRE(text.rfind("<svg", 0) == 0);
    REQUIRE(text.find("<path fill=\"#000000\"") != std::string::npos);
    REQUIRE(text.size() > 2000);
    REQUIRE(!WriteQrCodeSvg("x", Join(dir, "no/such/folder/x.svg")).Ok());
    UltraStore::PromoCode code;
    code.code = "ANNA-20";
    code.kind = UltraStore::CodeKind::Affiliate;
    code.affiliate = "aff-anna";
    code.discountPercent = 20;
    code.apps = {"org.ultraos.hello"};
    code.validUntil = "2027-06-30";
    const std::string page = CodeLandingPage(code, {HelloApp(), Runtime()}, "https://store.example/v1");
    REQUIRE(page.find("<code>ANNA-20</code>") != std::string::npos);
    REQUIRE(page.find("20% off") != std::string::npos);
    REQUIRE(page.find("<li>Hello</li>") != std::string::npos);
    REQUIRE(page.find("UltraCanvas runtime") == std::string::npos);   // not one of the code's apps
    REQUIRE(page.find("ultrastore://code/ANNA-20") != std::string::npos);
    REQUIRE(page.find("2027-06-30") != std::string::npos);
    RemoveDir(dir);
}

TEST(the_catalog_carries_the_enabled_codes_and_publish_writes_their_pages) {
    const std::string dir = TempDir("codes-publish");
    AdminDatabase db;
    REQUIRE(db.Open(Join(dir, "admin.sqlite")).Ok());
    REQUIRE(db.SetSetting("catalog.base_url", "https://store.example/v1").Ok());
    REQUIRE(db.PutPublisher({"ultra-os-project", "ULTRA OS Project", ""}).Ok());
    REQUIRE(db.PutComponent(HelloApp()).Ok());
    PackageFile f;
    f.sha256 = std::string(64, 'a');
    f.file = Join(dir, "hello.zip");
    f.size = 9;
    f.format = "zip";
    REQUIRE(WriteFile(f.file, "zip bytes"));
    REQUIRE(db.PutPackageFile(f).Ok());
    REQUIRE(db.PutAffiliate(Aff("aff-anna", "Anna", 20, 0)).Ok());
    REQUIRE(db.PutCode(Code("anna-20", UltraStore::CodeKind::Affiliate, "aff-anna", 20)).Ok());
    CodeRecord off = Code("off", UltraStore::CodeKind::Discount, "", 5);
    off.enabled = false;
    REQUIRE(db.PutCode(off).Ok());
    CodeRecord stale = Code("stale", UltraStore::CodeKind::Discount, "", 5);
    stale.code.apps = {"org.ultraos.gone"};
    REQUIRE(db.PutCode(stale).Ok());
    std::vector<std::string> warnings;
    const auto built = BuildCatalog(db, PublishOptions{}, &warnings);
    REQUIRE(built.Ok());
    REQUIRE_EQ(built.Value().codes.size(), size_t(2));   // the disabled one is left out
    REQUIRE(built.Value().FindCode("anna-20") != nullptr);
    REQUIRE(built.Value().FindCode("off") == nullptr);
    bool warnedStale = false;
    for (const std::string& w : warnings) if (w.find("STALE") != std::string::npos && w.find("org.ultraos.gone") != std::string::npos) warnedStale = true;
    REQUIRE(warnedStale);
    // An affiliate switched off takes their codes out of the catalog.
    Affiliate anna = db.GetAffiliate("aff-anna").Value();
    anna.enabled = false;
    REQUIRE(db.PutAffiliate(anna).Ok());
    REQUIRE(BuildCatalog(db, PublishOptions{}).Value().FindCode("anna-20") == nullptr);
    anna.enabled = true;
    REQUIRE(db.PutAffiliate(anna).Ok());
    // Publish writes go/<CODE>.html for each code in the catalog.
    if (!UltraCrypt_IsAvailable()) SKIP("no crypto backend");
    const auto key = NewSigningKey("pass", KeyCost::Fast());
    const auto added = db.AddKey("test", key.Value().publicKeyHex, key.Value().sealed.Bytes());
    PublishOptions options;
    options.outDirUtf8 = Join(dir, "out");
    options.mediaRootUtf8 = dir;
    const auto report = Publish(db, added.Value().id, "pass", options);
    REQUIRE(report.Ok());
    REQUIRE_EQ(report.Value().codePages, size_t(2));
    const std::string page = ReadFile(Join(options.outDirUtf8, "go/ANNA-20.html"));
    REQUIRE(page.find("ultrastore://code/ANNA-20") != std::string::npos);
    REQUIRE(page.find("<li>Hello</li>") != std::string::npos);
    REQUIRE(std::filesystem::exists(UltraCanvas::PathFromUtf8(Join(options.outDirUtf8, "go/STALE.html"))));
    RemoveDir(dir);
}
