// Tests/UltraStoreAdmin/test_finance.cpp
// The books from the ledger: a reversal takes a partner's credit back in
// proportion, the payment service's reversals go into the ledger once, a
// payout lowers what is owed, and the report adds it all up by period, app,
// month and partner - and writes it as CSV.
// Version: 0.2.0 - a range period, the weeks, the dates
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "admin_helpers.h"

#include "Affiliates.h"
#include "Finance.h"

using namespace UltraStoreAdmin;
using namespace admin_test;

namespace {

Affiliate Aff(const std::string& id, const std::string& name, int share, int recruitShare, const std::string& by = {}) {
    Affiliate a;
    a.id = id; a.name = name; a.sharePercent = share; a.recruitSharePercent = recruitShare; a.recruitedBy = by;
    return a;
}

CodeRecord Code(const std::string& code, const std::string& affiliate, int percent) {
    CodeRecord r;
    r.code.code = code; r.code.kind = UltraStore::CodeKind::Affiliate; r.code.affiliate = affiliate; r.code.discountPercent = percent;
    return r;
}

} // namespace

TEST(a_reversal_takes_the_partners_credits_back_in_proportion) {
    const std::string dir = TempDir("finance-reversal");
    AdminDatabase db;
    REQUIRE(db.Open(Join(dir, "admin.sqlite")).Ok());
    REQUIRE(db.PutAffiliate(Aff("aff-super", "Super", 10, 5)).Ok());
    REQUIRE(db.PutAffiliate(Aff("aff-anna", "Anna", 20, 0, "aff-super")).Ok());
    REQUIRE(db.PutCode(Code("ANNA-20", "aff-anna", 20)).Ok());
    const auto sale = RecordSale(db, "ANNA-20", "org.ultraos.hello", "23.92", "EUR", "2026-10-05T12:00:00Z", "ord1");
    REQUIRE(sale.Ok());
    REQUIRE_EQ(sale.Value().affiliateCredit, std::string("4.78"));
    REQUIRE_EQ(sale.Value().recruiterCredit, std::string("1.20"));
    REQUIRE(db.SaleByReference("ord1").Ok());
    REQUIRE(!db.SaleByReference("nope").Ok());

    // Half the money back: half the credits back, to the cent, half up.
    const auto half = RecordReversal(db, "refund", "ord1", "", "11.96", "EUR", "2026-10-20T09:00:00Z", "ord1/r1");
    REQUIRE(half.Ok());
    REQUIRE_EQ(half.Value().app, std::string("org.ultraos.hello"));   // from the sale
    REQUIRE_EQ(half.Value().affiliate, std::string("aff-anna"));
    REQUIRE_EQ(half.Value().affiliateCredit, std::string("2.39"));
    REQUIRE_EQ(half.Value().recruiter, std::string("aff-super"));
    REQUIRE_EQ(half.Value().recruiterCredit, std::string("0.60"));
    // More than was paid takes back no more than was credited; a sale the ledger
    // does not know takes nothing back; the kind and the amount are checked.
    const auto whole = RecordReversal(db, "chargeback", "ord1", "", "99.00", "EUR", "2026-10-21T09:00:00Z", "ord1/cb", true);
    REQUIRE(whole.Ok());
    REQUIRE_EQ(whole.Value().affiliateCredit, std::string("4.78"));
    REQUIRE(whole.Value().revoked);
    // A sale recorded by hand has no reference: its number in the ledger names it.
    const auto byHand = RecordSale(db, "ANNA-20", "org.ultraos.hello", "10.00", "EUR", "2026-10-06T12:00:00Z");
    REQUIRE(byHand.Ok());
    const auto byNumber = RecordReversal(db, "refund", std::to_string(byHand.Value().id), "", "10.00", "EUR", "2026-10-22T09:00:00Z");
    REQUIRE(byNumber.Ok());
    REQUIRE_EQ(byNumber.Value().app, std::string("org.ultraos.hello"));
    REQUIRE_EQ(byNumber.Value().affiliateCredit, std::string("2.00"));
    const auto unknown = RecordReversal(db, "refund", "ord-x", "org.ultraos.other", "5.00", "EUR");
    REQUIRE(unknown.Ok());
    REQUIRE(unknown.Value().affiliate.empty());
    REQUIRE_EQ(unknown.Value().affiliateCredit, std::string("0.00"));
    REQUIRE(!RecordReversal(db, "gift", "ord1", "", "1.00", "EUR").Ok());
    REQUIRE(!RecordReversal(db, "refund", "ord1", "", "abc", "EUR").Ok());
    REQUIRE_EQ(db.Reversals().Value().size(), size_t(4));
    REQUIRE(db.HasReversalReference("ord1/cb"));
    REQUIRE(!db.HasReversalReference(""));
    RemoveDir(dir);
}

TEST(the_reversals_of_the_payment_service_go_into_the_ledger_once) {
    const std::string dir = TempDir("finance-sync");
    AdminDatabase db;
    REQUIRE(db.Open(Join(dir, "admin.sqlite")).Ok());
    REQUIRE(db.PutAffiliate(Aff("aff-anna", "Anna", 20, 0)).Ok());
    REQUIRE(db.PutCode(Code("ANNA-20", "aff-anna", 20)).Ok());
    REQUIRE(RecordSale(db, "ANNA-20", "org.ultraos.hello", "23.92", "EUR", "2026-10-05T12:00:00Z", "ord1").Ok());
    std::vector<std::string> asked;
    const std::string page = R"({"reversals": [
        {"seq": 1, "order_id": "ord1", "reference": "ord1/5", "reverses": "ord1", "kind": "refund", "at": "2026-10-20T09:00:00Z", "app": "org.ultraos.hello", "amount": "23.92", "currency": "EUR", "revoked": true},
        {"seq": 2, "order_id": "ord9", "reverses": "ord9", "kind": "chargeback", "at": "2026-10-21T09:00:00Z", "app": "org.ultraos.other", "amount": "9.90", "currency": "EUR", "revoked": true}], "next": 2})";
    const std::string empty = R"({"reversals": [], "next": 2})";
    auto fetch = [&](const std::string& url, const std::string& token) -> UltraStore::Result<std::string> {
        asked.push_back(url + " " + token);
        if (url.find("since=0") != std::string::npos) return UltraStore::Result<std::string>::Succeed(page);
        return UltraStore::Result<std::string>::Succeed(empty);
    };
    REQUIRE(!SyncReversals(db, "", "t", fetch).Ok());
    const auto first = SyncReversals(db, "https://pay.example/", "admin-token", fetch);
    REQUIRE(first.Ok());
    REQUIRE_EQ(first.Value().recorded, size_t(2));
    REQUIRE_EQ(first.Value().sequence, uint64_t(2));
    REQUIRE_EQ(asked[0], std::string("https://pay.example/v1/reversals?since=0 admin-token"));
    REQUIRE_EQ(db.Setting("payment.reversals_seq"), std::string("2"));
    const auto all = db.Reversals().Value();
    REQUIRE_EQ(all.size(), size_t(2));
    REQUIRE_EQ(all[0].affiliateCredit, std::string("4.78"));   // the whole sale back: the whole credit back
    REQUIRE_EQ(all[1].reference, std::string("ord9/rev2"));    // an older service names no reference of its own
    REQUIRE_EQ(all[1].kind, std::string("chargeback"));
    REQUIRE(db.SetSetting("payment.reversals_seq", "0").Ok());
    const auto again = SyncReversals(db, "https://pay.example", "admin-token", fetch);
    REQUIRE_EQ(again.Value().recorded, size_t(0));
    REQUIRE_EQ(again.Value().skipped, size_t(2));
    RemoveDir(dir);
}

TEST(the_report_adds_up_by_period_app_month_and_partner) {
    const std::string dir = TempDir("finance-report");
    AdminDatabase db;
    REQUIRE(db.Open(Join(dir, "admin.sqlite")).Ok());
    REQUIRE(db.PutAffiliate(Aff("aff-super", "Super", 10, 5)).Ok());
    REQUIRE(db.PutAffiliate(Aff("aff-anna", "Anna", 20, 0, "aff-super")).Ok());
    REQUIRE(db.PutCode(Code("ANNA-20", "aff-anna", 20)).Ok());
    // October: two sales of hello (one with Anna's code), one of other; a refund of the other.
    REQUIRE(RecordSale(db, "ANNA-20", "org.ultraos.hello", "23.92", "EUR", "2026-10-05T12:00:00Z", "o1").Ok());   // Anna 4.78, Super 1.20
    REQUIRE(RecordSale(db, "", "org.ultraos.hello", "29.90", "EUR", "2026-10-06T12:00:00Z", "o2").Ok());
    REQUIRE(RecordSale(db, "", "org.ultraos.other", "9.90", "EUR", "2026-10-07T12:00:00Z", "o3").Ok());
    REQUIRE(RecordReversal(db, "refund", "o3", "", "9.90", "EUR", "2026-10-20T09:00:00Z", "o3/r").Ok());
    // September: one sale in dollars; November: one more of hello with the code, and half of it back.
    REQUIRE(RecordSale(db, "", "org.ultraos.hello", "31.00", "USD", "2026-09-15T12:00:00Z", "o0").Ok());
    REQUIRE(RecordSale(db, "ANNA-20", "org.ultraos.hello", "23.92", "EUR", "2026-11-02T12:00:00Z", "o4").Ok());
    REQUIRE(RecordReversal(db, "refund", "o4", "", "11.96", "EUR", "2026-11-03T09:00:00Z", "o4/r").Ok());   // Anna 2.39, Super 0.60 back
    REQUIRE(RecordPayout(db, "aff-anna", "4.00", "EUR", "bank 1", "2026-10-31T10:00:00Z").Ok());
    REQUIRE(!RecordPayout(db, "aff-nobody", "4.00", "EUR").Ok());
    REQUIRE(!RecordPayout(db, "aff-anna", "0", "EUR").Ok());

    // October alone.
    const auto oct = BuildFinanceReport(db, "2026-10");
    REQUIRE(oct.Ok());
    const FinanceReport& r = oct.Value();
    REQUIRE_EQ(r.totals.sales, size_t(3));
    REQUIRE_EQ(r.totals.reversals, size_t(1));
    REQUIRE_EQ(MoneyOf(r.totals.paid, "EUR"), std::string("63.72"));
    REQUIRE_EQ(MoneyOf(r.totals.refunded, "EUR"), std::string("9.90"));
    REQUIRE_EQ(MoneyOf(r.totals.credits, "EUR"), std::string("5.98"));
    REQUIRE_EQ(MoneyOf(r.totals.net, "EUR"), std::string("47.84"));   // 63.72 - 9.90 - 5.98
    REQUIRE_EQ(r.mainCurrency, std::string("EUR"));
    REQUIRE_EQ(r.apps.size(), size_t(2));
    REQUIRE_EQ(r.apps[0].app, std::string("org.ultraos.hello"));   // the larger net first
    REQUIRE_EQ(MoneyOf(r.apps[0].totals.net, "EUR"), std::string("47.84"));
    REQUIRE_EQ(MoneyOf(r.apps[1].totals.net, "EUR"), std::string("0.00"));   // sold and refunded
    REQUIRE_EQ(r.months.size(), size_t(1));                                   // a month's report has one month
    REQUIRE_EQ(r.months[0].month, std::string("2026-10"));
    REQUIRE_EQ(r.reversals.size(), size_t(1));
    // The partners: Anna earned 4.78 in October; all time 9.56 earned, 2.39 taken back, 4.00 paid out, 3.17 owed.
    const PartnerBalance* anna = nullptr;
    const PartnerBalance* super_ = nullptr;
    for (const PartnerBalance& p : r.partners) { if (p.affiliate.id == "aff-anna") anna = &p; if (p.affiliate.id == "aff-super") super_ = &p; }
    REQUIRE(anna && super_);
    REQUIRE_EQ(anna->sales, size_t(1));
    REQUIRE_EQ(MoneyOf(anna->inPeriod, "EUR"), std::string("4.78"));
    REQUIRE_EQ(MoneyOf(anna->earned, "EUR"), std::string("9.56"));
    REQUIRE_EQ(MoneyOf(anna->takenBack, "EUR"), std::string("2.39"));
    REQUIRE_EQ(MoneyOf(anna->paidOut, "EUR"), std::string("4.00"));
    REQUIRE_EQ(MoneyOf(anna->owed, "EUR"), std::string("3.17"));
    REQUIRE_EQ(MoneyOf(super_->owed, "EUR"), std::string("1.80"));   // 2.40 - 0.60

    // All time: both currencies, twelve months ending with November, the year filter.
    const auto all = BuildFinanceReport(db, "");
    REQUIRE(all.Ok());
    REQUIRE_EQ(all.Value().totals.sales, size_t(5));
    REQUIRE_EQ(MoneyOf(all.Value().totals.paid, "USD"), std::string("31.00"));
    REQUIRE_EQ(MoneyOf(all.Value().totals.paid, "EUR"), std::string("87.64"));
    REQUIRE_EQ(MoneyOf(all.Value().totals.net, "EUR"), std::string("56.81"));   // 87.64 - 21.86 - (5.98 + 5.98 - 2.99)
    REQUIRE_EQ(all.Value().months.size(), size_t(12));
    REQUIRE_EQ(all.Value().months.back().month, std::string("2026-11"));
    REQUIRE_EQ(all.Value().months.front().month, std::string("2025-12"));
    REQUIRE_EQ(MoneyOf(all.Value().months[9].totals.net, "USD"), std::string("31.00"));   // September
    const auto year = BuildFinanceReport(db, "2026");
    REQUIRE_EQ(year.Value().totals.sales, size_t(5));
    REQUIRE_EQ(year.Value().months.size(), size_t(11));   // January to November: the year's months only
    const auto none = BuildFinanceReport(db, "2027-03");
    REQUIRE_EQ(none.Value().totals.sales, size_t(0));
    REQUIRE_EQ(none.Value().months.size(), size_t(1));
    REQUIRE_EQ(none.Value().months[0].month, std::string("2027-03"));
    // A month with the money going back out shows a negative net, with its sign.
    REQUIRE(RecordReversal(db, "refund", "o2", "", "29.90", "EUR", "2026-12-01T09:00:00Z", "o2/r").Ok());
    const auto dec = BuildFinanceReport(db, "2026-12");
    REQUIRE_EQ(MoneyOf(dec.Value().totals.net, "EUR"), std::string("-29.90"));

    const auto periods = FinancePeriods(db);
    REQUIRE(periods.Ok());
    REQUIRE_EQ(periods.Value().size(), size_t(4));
    REQUIRE_EQ(periods.Value()[0], std::string("2026-12"));
    REQUIRE_EQ(periods.Value()[3], std::string("2026-09"));
    REQUIRE_EQ(PeriodName("2026-10"), std::string("October 2026"));
    REQUIRE_EQ(PeriodName(""), std::string("All time"));

    const std::string csv = StatementCsv(r);
    REQUIRE(csv.find("statement,October 2026\n") == 0);
    REQUIRE(csv.find("EUR,3,63.72,1,9.90,5.98,47.84\n") != std::string::npos);
    REQUIRE(csv.find("org.ultraos.hello,EUR,2,53.82,0.00,5.98,47.84\n") != std::string::npos);
    REQUIRE(csv.find("aff-anna,Anna,EUR,4.78,9.56,2.39,4.00,3.17\n") != std::string::npos);
    REQUIRE(csv.find("2026-10,EUR,3,63.72,9.90,5.98,47.84\n") != std::string::npos);
    RemoveDir(dir);
}

TEST(a_range_period_the_weeks_and_the_dates) {
    REQUIRE_EQ(DaysAgoIso(30, "2026-11-05"), std::string("2026-10-06"));
    REQUIRE_EQ(DaysAgoIso(0, "2026-03-01"), std::string("2026-03-01"));
    REQUIRE_EQ(DaysAgoIso(1, "2026-03-01"), std::string("2026-02-28"));
    REQUIRE_EQ(DaysAgoIso(1, "2024-03-01"), std::string("2024-02-29"));
    REQUIRE(DaysAgoIso(1, "nonsense").empty());
    REQUIRE_EQ(WeekStartOf("2026-11-05T12:00:00Z"), std::string("2026-11-02"));   // a Thursday: its Monday
    REQUIRE_EQ(WeekStartOf("2026-11-02"), std::string("2026-11-02"));             // a Monday stays
    REQUIRE_EQ(WeekStartOf("2026-11-01"), std::string("2026-10-26"));             // a Sunday: the Monday before
    REQUIRE_EQ(WeekStartOf("1970-01-01"), std::string("1969-12-29"));             // day zero was a Thursday
    REQUIRE(InPeriod("2026-10-06T00:00:00Z", "2026-10-06..2026-11-05"));
    REQUIRE(InPeriod("2026-11-04T23:59:59Z", "2026-10-06..2026-11-05"));
    REQUIRE(!InPeriod("2026-11-05T00:00:00Z", "2026-10-06..2026-11-05"));
    REQUIRE(!InPeriod("2026-10-05T23:00:00Z", "2026-10-06..2026-11-05"));
    REQUIRE(InPeriod("2026-10-05T23:00:00Z", "..2026-11-05"));
    REQUIRE_EQ(PeriodName("2026-10-06..2026-11-05"), std::string("2026-10-06 to 2026-11-05"));

    const std::string dir = TempDir("finance-weeks");
    AdminDatabase db;
    REQUIRE(db.Open(Join(dir, "admin.sqlite")).Ok());
    REQUIRE(RecordSale(db, "", "org.ultraos.hello", "10.00", "EUR", "2026-10-26T12:00:00Z", "w1").Ok());   // Monday, week of 26 Oct
    REQUIRE(RecordSale(db, "", "org.ultraos.hello", "20.00", "EUR", "2026-11-01T12:00:00Z", "w2").Ok());   // Sunday, the same week
    REQUIRE(RecordSale(db, "", "org.ultraos.hello", "5.00", "EUR", "2026-11-03T12:00:00Z", "w3").Ok());    // the week of 2 Nov
    REQUIRE(RecordReversal(db, "refund", "w1", "", "10.00", "EUR", "2026-11-04T09:00:00Z", "w1/r").Ok());
    REQUIRE(RecordSale(db, "", "org.ultraos.hello", "99.00", "EUR", "2026-08-01T12:00:00Z", "old").Ok());  // before the window
    const auto weeks = WeeklyFinance(db, 4, "2026-11-05");
    REQUIRE(weeks.Ok());
    REQUIRE_EQ(weeks.Value().size(), size_t(4));
    REQUIRE_EQ(weeks.Value()[0].weekStart, std::string("2026-10-12"));
    REQUIRE_EQ(weeks.Value()[3].weekStart, std::string("2026-11-02"));
    REQUIRE_EQ(MoneyOf(weeks.Value()[2].totals.paid, "EUR"), std::string("30.00"));
    REQUIRE_EQ(weeks.Value()[2].totals.sales, size_t(2));
    REQUIRE_EQ(MoneyOf(weeks.Value()[3].totals.paid, "EUR"), std::string("5.00"));
    REQUIRE_EQ(MoneyOf(weeks.Value()[3].totals.refunded, "EUR"), std::string("10.00"));
    REQUIRE_EQ(MoneyOf(weeks.Value()[3].totals.net, "EUR"), std::string("-5.00"));
    REQUIRE_EQ(MoneyOf(weeks.Value()[0].totals.paid, "EUR"), std::string("0.00"));
    const auto last30 = BuildFinanceReport(db, "2026-10-06..2026-11-05");
    REQUIRE(last30.Ok());
    REQUIRE_EQ(last30.Value().totals.sales, size_t(3));
    REQUIRE_EQ(MoneyOf(last30.Value().totals.paid, "EUR"), std::string("35.00"));
    REQUIRE(!WeeklyFinance(db, 4, "nonsense").Ok());
    const auto updates = db.ComponentUpdates();
    REQUIRE(updates.Ok());
    REQUIRE(updates.Value().empty());
    RemoveDir(dir);
}
