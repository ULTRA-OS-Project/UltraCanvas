// Apps/UltraStoreAdmin/core/Finance.cpp
// Version: 0.2.0 - a period as a date range, the weeks, DaysAgoIso
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "Finance.h"

#include "DataFormats/UltraCanvasJSON.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <set>

using UltraStore::ErrorCode;

namespace UltraStoreAdmin {

namespace {

int64_t CentsOrZero(const std::string& decimal) {
    int64_t cents = 0;
    bool negative = !decimal.empty() && decimal[0] == '-';
    if (!DecimalCents(negative ? decimal.substr(1) : decimal, cents)) return 0;
    return negative ? -cents : cents;
}

// A decimal from cents, two places, with its sign ("-1.50"): the ledger's
// own DecimalFromCents is for amounts, which are never negative.
std::string FormatCents(int64_t cents) {
    return cents < 0 ? "-" + DecimalFromCents(-cents) : DecimalFromCents(cents);
}

std::string TrimSlash(std::string s) {
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

// cents * part / whole, half up, never above cents.
int64_t Proportion(int64_t cents, int64_t part, int64_t whole) {
    if (whole <= 0 || part <= 0 || cents <= 0) return 0;
    if (part >= whole) return cents;
    return std::min(cents, (cents * part * 2 + whole) / (2 * whole));
}

std::string Negated(const std::string& decimal) {
    return decimal.empty() || decimal == "0.00" ? decimal : (decimal[0] == '-' ? decimal.substr(1) : "-" + decimal);
}

void AddTotals(FinanceTotals& t, const SaleRecord& s) {
    ++t.sales;
    AddMoney(t.paid, s.currency, s.amount);
    AddMoney(t.credits, s.currency, s.affiliateCredit);
    AddMoney(t.credits, s.currency, s.recruiterCredit);
}

void AddTotals(FinanceTotals& t, const ReversalRecord& v) {
    ++t.reversals;
    AddMoney(t.refunded, v.currency, v.amount);
    AddMoney(t.credits, v.currency, Negated(v.affiliateCredit));
    AddMoney(t.credits, v.currency, Negated(v.recruiterCredit));
}

void FinishTotals(FinanceTotals& t) {
    std::vector<std::string> currencies;
    for (const auto& [c, _] : t.paid) currencies.push_back(c);
    for (const auto& [c, _] : t.refunded) if (std::find(currencies.begin(), currencies.end(), c) == currencies.end()) currencies.push_back(c);
    for (const auto& [c, _] : t.credits) if (std::find(currencies.begin(), currencies.end(), c) == currencies.end()) currencies.push_back(c);
    t.net.clear();
    for (const std::string& c : currencies) {
        AddMoney(t.net, c, SubtractDecimals(SubtractDecimals(MoneyOf(t.paid, c), MoneyOf(t.refunded, c)), MoneyOf(t.credits, c)));
    }
}

std::string PreviousMonth(const std::string& month) {   // "2026-01" -> "2025-12"
    int year = std::atoi(month.substr(0, 4).c_str()), m = std::atoi(month.substr(5, 2).c_str());   // locale-ok: digits only
    if (--m < 1) { m = 12; --year; }
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d", year, m);
    return buffer;
}

const char* const kMonthNames[12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

std::string CsvField(const std::string& s) {
    if (s.find_first_of(",\"\n") == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s) { if (c == '"') out += '"'; out += c; }
    return out + "\"";
}

} // namespace

// ----- money -----

void AddMoney(ByCurrency& sums, const std::string& currency, const std::string& decimal) {
    const int64_t add = CentsOrZero(decimal);
    for (auto& [c, sum] : sums) {
        if (c == currency) { sum = FormatCents(CentsOrZero(sum) + add); return; }
    }
    sums.emplace_back(currency, FormatCents(add));
}

std::string MoneyOf(const ByCurrency& sums, const std::string& currency) {
    for (const auto& [c, sum] : sums) if (c == currency) return sum;
    return "0.00";
}

std::string JoinMoney(const ByCurrency& sums) {
    std::string out;
    for (const auto& [c, sum] : sums) out += (out.empty() ? "" : ", ") + sum + " " + c;
    return out.empty() ? std::string("0.00") : out;
}

std::string SubtractDecimals(const std::string& a, const std::string& b) {
    return FormatCents(CentsOrZero(a) - CentsOrZero(b));
}

// ----- the records -----

Result<ReversalRecord> RecordReversal(AdminDatabase& db, const std::string& kind, const std::string& reverses,
                                      const std::string& app, const std::string& amount, const std::string& currency,
                                      const std::string& at, const std::string& reference, bool revoked) {
    using R = Result<ReversalRecord>;
    int64_t cents = 0;
    if (!DecimalCents(amount, cents)) return R::Failure(ErrorCode::InvalidArgument, "The amount is a decimal such as 29.90", amount);
    if (currency.size() != 3) return R::Failure(ErrorCode::InvalidArgument, "The currency is a 3-letter code", currency);
    if (kind != "refund" && kind != "chargeback") return R::Failure(ErrorCode::InvalidArgument, "A reversal is a refund or a chargeback", kind);
    ReversalRecord v;
    v.at = at;
    v.kind = kind;
    v.reference = reference;
    v.reverses = reverses;
    v.app = app;
    v.amount = DecimalFromCents(cents);
    v.currency = currency;
    v.affiliateCredit = v.recruiterCredit = "0.00";
    v.revoked = revoked;
    // The sale it takes back: by the payment service's reference, or, for a
    // sale recorded by hand (which has none), by its number in the ledger.
    auto sale = db.SaleByReference(reverses);
    if (!sale && !reverses.empty() && reverses.find_first_not_of("0123456789") == std::string::npos) {
        if (const auto all = db.Sales()) {
            const int64_t id = std::atoll(reverses.c_str());   // locale-ok: digits only
            for (const SaleRecord& s : all.Value()) if (s.id == id) { sale = Result<SaleRecord>::Succeed(s); break; }
        }
    }
    if (sale) {
        const SaleRecord& s = sale.Value();
        if (!s.reference.empty()) v.reverses = s.reference;
        if (v.app.empty()) v.app = s.app;
        if (s.currency == currency) {
            int64_t paid = 0, affiliateCredit = 0, recruiterCredit = 0;
            DecimalCents(s.amount, paid);
            DecimalCents(s.affiliateCredit, affiliateCredit);
            DecimalCents(s.recruiterCredit, recruiterCredit);
            v.affiliate = s.affiliate;
            v.recruiter = s.recruiter;
            v.affiliateCredit = DecimalFromCents(Proportion(affiliateCredit, cents, paid));
            v.recruiterCredit = DecimalFromCents(Proportion(recruiterCredit, cents, paid));
        }
    }
    return db.AddReversal(v);
}

Result<SyncReport> SyncReversals(AdminDatabase& db, const std::string& paymentUrl, const std::string& token, const SalesFetcher& fetch) {
    using R = Result<SyncReport>;
    if (paymentUrl.empty()) return R::Failure(ErrorCode::InvalidArgument, "No payment service URL (the setting payment.url)");
    if (token.empty()) return R::Failure(ErrorCode::InvalidArgument, "No admin token for the payment service (the setting payment.token)");
    SyncReport report;
    uint64_t since = std::strtoull(db.Setting("payment.reversals_seq", "0").c_str(), nullptr, 10);   // locale-ok: digits only
    report.sequence = since;
    for (int page = 0; page < 1000; ++page) {
        const auto body = fetch(TrimSlash(paymentUrl) + "/v1/reversals?since=" + std::to_string(since), token);
        if (!body) return R::Failure(body.error.code, body.error.message, body.error.detail);
        UltraCanvas::JSONParseResult parse;
        const UltraCanvas::JSONValue v = UltraCanvas::JSON::Parse(body.Value(), &parse);
        if (!parse.success || !v.IsObject()) return R::Failure(ErrorCode::ParseError, "The payment service's reversals are not JSON", parse.errorMessage);
        const UltraCanvas::JSONValue& items = v.Get("reversals");
        if (items.GetSize() == 0) break;
        for (size_t i = 0; i < items.GetSize(); ++i) {
            const UltraCanvas::JSONValue& r = items.At(i);
            const uint64_t seq = static_cast<uint64_t>(std::max<int64_t>(0, r.Get("seq").GetInteger(0)));
            // The reversal's own reference; an older service names the order and the sequence.
            std::string reference = r.Get("reference").GetString();
            if (reference.empty()) reference = r.Get("order_id").GetString() + "/rev" + std::to_string(seq);
            if (db.HasReversalReference(reference)) { ++report.skipped; }
            else {
                std::string kind = r.Get("kind").GetString();
                if (kind != "chargeback") kind = "refund";
                const auto recorded = RecordReversal(db, kind, r.Get("reverses").GetString(), r.Get("app").GetString(), r.Get("amount").GetString(),
                                                     r.Get("currency").GetString(), r.Get("at").GetString(), reference, r.Get("revoked").GetBoolean(false));
                if (!recorded) return R::Failure(recorded.error.code, "A reversal could not be recorded: " + recorded.error.message, recorded.error.detail);
                ++report.recorded;
            }
            if (seq > report.sequence) report.sequence = seq;
        }
        const uint64_t next = static_cast<uint64_t>(std::max<int64_t>(0, v.Get("next").GetInteger(0)));
        if (next <= since) break;
        since = next;
        if (const auto kept = db.SetSetting("payment.reversals_seq", std::to_string(since)); !kept) return R::Failure(kept.error.code, kept.error.message, kept.error.detail);
    }
    if (report.sequence > 0) db.SetSetting("payment.reversals_seq", std::to_string(report.sequence));
    return R::Succeed(report);
}

Result<PayoutRecord> RecordPayout(AdminDatabase& db, const std::string& affiliate, const std::string& amount, const std::string& currency,
                                  const std::string& note, const std::string& at) {
    using R = Result<PayoutRecord>;
    int64_t cents = 0;
    if (!DecimalCents(amount, cents) || cents <= 0) return R::Failure(ErrorCode::InvalidArgument, "The amount is a decimal above zero, such as 112.40", amount);
    if (currency.size() != 3) return R::Failure(ErrorCode::InvalidArgument, "The currency is a 3-letter code", currency);
    if (const auto a = db.GetAffiliate(affiliate); !a) return R::Failure(a.error.code, a.error.message, a.error.detail);
    PayoutRecord p;
    p.at = at;
    p.affiliate = affiliate;
    p.amount = DecimalFromCents(cents);
    p.currency = currency;
    p.note = note;
    return db.AddPayout(p);
}

// ----- the report -----

namespace {
// Days since 1970-01-01 of a civil date (Howard Hinnant's algorithm); the
// inverse for the way back. Enough for the ledger's dates, no time zones.
int64_t DaysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
std::string CivilFromDays(int64_t z) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t y = yoe + era * 400;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    const int64_t d = doy - (153 * mp + 2) / 5 + 1;
    const int64_t m = mp + (mp < 10 ? 3 : -9);
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%04lld-%02lld-%02lld", static_cast<long long>(y + (m <= 2)), static_cast<long long>(m), static_cast<long long>(d));
    return buffer;
}
bool DaysOf(const std::string& isoDate, int64_t& days) {
    if (isoDate.size() < 10 || isoDate[4] != '-' || isoDate[7] != '-') return false;
    const int y = std::atoi(isoDate.substr(0, 4).c_str()), m = std::atoi(isoDate.substr(5, 2).c_str()), d = std::atoi(isoDate.substr(8, 2).c_str());   // locale-ok: digits only
    if (m < 1 || m > 12 || d < 1 || d > 31) return false;
    days = DaysFromCivil(y, m, d);
    return true;
}
} // namespace

bool InPeriod(const std::string& isoDate, const std::string& period) {
    if (period.empty()) return true;
    if (const size_t dots = period.find(".."); dots != std::string::npos) {
        const std::string from = period.substr(0, dots), to = period.substr(dots + 2);
        const std::string day = isoDate.substr(0, 10);
        return (from.empty() || day >= from) && (to.empty() || day < to);
    }
    return isoDate.compare(0, period.size(), period) == 0;
}

std::string DaysAgoIso(int days, const std::string& todayIso) {
    int64_t today = 0;
    if (!DaysOf(todayIso.empty() ? NowIso8601() : todayIso, today)) return {};
    return CivilFromDays(today - days);
}

std::string WeekStartOf(const std::string& isoDate) {
    int64_t days = 0;
    if (!DaysOf(isoDate, days)) return {};
    // 1970-01-01 was a Thursday: day 0 is weekday 3 with Monday as 0.
    const int64_t weekday = ((days % 7) + 7 + 3) % 7;
    return CivilFromDays(days - weekday);
}

std::string MonthOf(const std::string& isoDate) {
    return isoDate.size() >= 7 ? isoDate.substr(0, 7) : std::string();
}

std::string PeriodName(const std::string& period) {
    if (period.empty()) return "All time";
    if (const size_t dots = period.find(".."); dots != std::string::npos) return period.substr(0, dots) + " to " + period.substr(dots + 2);
    if (period.size() == 4) return period;
    const int m = std::atoi(period.substr(5, 2).c_str());   // locale-ok: digits only
    if (m < 1 || m > 12) return period;
    return std::string(kMonthNames[m - 1]) + " " + period.substr(0, 4);
}

Result<FinanceReport> BuildFinanceReport(const AdminDatabase& db, const std::string& period) {
    using R = Result<FinanceReport>;
    const auto sales = db.Sales();
    if (!sales) return R::Failure(sales.error.code, sales.error.message, sales.error.detail);
    const auto reversals = db.Reversals();
    if (!reversals) return R::Failure(reversals.error.code, reversals.error.message, reversals.error.detail);
    const auto payouts = db.Payouts();
    if (!payouts) return R::Failure(payouts.error.code, payouts.error.message, payouts.error.detail);
    const auto affiliates = db.Affiliates();
    if (!affiliates) return R::Failure(affiliates.error.code, affiliates.error.message, affiliates.error.detail);

    FinanceReport out;
    out.period = period;
    std::map<std::string, FinanceTotals> byApp;
    std::map<std::string, FinanceTotals> byMonth;
    std::map<std::string, size_t> salesByCurrency;
    std::string lastMonth;
    for (const SaleRecord& s : sales.Value()) {
        const std::string month = MonthOf(s.at);
        if (!InPeriod(s.at, period)) continue;
        AddTotals(out.totals, s);
        AddTotals(byApp[s.app], s);
        AddTotals(byMonth[month], s);
        ++salesByCurrency[s.currency];
        if (month > lastMonth) lastMonth = month;
    }
    for (const ReversalRecord& v : reversals.Value()) {
        const std::string month = MonthOf(v.at);
        if (!InPeriod(v.at, period)) continue;
        AddTotals(out.totals, v);
        AddTotals(byApp[v.app], v);
        AddTotals(byMonth[month], v);
        out.reversals.push_back(v);
        if (month > lastMonth) lastMonth = month;
    }
    std::reverse(out.reversals.begin(), out.reversals.end());
    FinishTotals(out.totals);
    for (const auto& [currency, count] : salesByCurrency) {
        if (out.mainCurrency.empty() || count > salesByCurrency[out.mainCurrency]) out.mainCurrency = currency;
    }

    for (auto& [app, totals] : byApp) {
        FinishTotals(totals);
        out.apps.push_back({app, totals});
    }
    const std::string main = out.mainCurrency;
    std::stable_sort(out.apps.begin(), out.apps.end(), [&main](const AppFinance& a, const AppFinance& b) {
        return CentsOrZero(MoneyOf(a.totals.net, main)) > CentsOrZero(MoneyOf(b.totals.net, main));
    });

    // The twelve months, oldest first, ending with the period's last month
    // that has data; a period with none ends at its own end, or today.
    std::string end = lastMonth;
    if (end.empty()) {
        if (period.size() == 7) end = period;
        else if (period.size() == 4) end = period + "-12";
        else end = MonthOf(NowIso8601());
    }
    std::vector<std::string> months;
    for (std::string m = end; months.size() < 12; m = PreviousMonth(m)) {
        if (period.size() == 4 && m.compare(0, 4, period) != 0) break;
        if (period.size() == 7 && m != period) break;
        months.push_back(m);
    }
    std::reverse(months.begin(), months.end());
    for (const std::string& m : months) {
        MonthNet mn;
        mn.month = m;
        if (const auto it = byMonth.find(m); it != byMonth.end()) mn.totals = it->second;
        FinishTotals(mn.totals);
        out.months.push_back(std::move(mn));
    }

    for (const Affiliate& a : affiliates.Value()) {
        PartnerBalance b;
        b.affiliate = a;
        for (const SaleRecord& s : sales.Value()) {
            const bool own = s.affiliate == a.id, recruit = s.recruiter == a.id;
            if (!own && !recruit) continue;
            const std::string credit = own ? s.affiliateCredit : s.recruiterCredit;
            AddMoney(b.earned, s.currency, credit);
            if (InPeriod(s.at, period)) { ++b.sales; AddMoney(b.inPeriod, s.currency, credit); }
        }
        for (const ReversalRecord& v : reversals.Value()) {
            if (v.affiliate == a.id) AddMoney(b.takenBack, v.currency, v.affiliateCredit);
            if (v.recruiter == a.id) AddMoney(b.takenBack, v.currency, v.recruiterCredit);
        }
        for (const PayoutRecord& p : payouts.Value()) {
            if (p.affiliate == a.id) AddMoney(b.paidOut, p.currency, p.amount);
        }
        std::vector<std::string> currencies;
        for (const ByCurrency* list : {&b.earned, &b.takenBack, &b.paidOut}) {
            for (const auto& [c, _] : *list) if (std::find(currencies.begin(), currencies.end(), c) == currencies.end()) currencies.push_back(c);
        }
        for (const std::string& c : currencies) {
            AddMoney(b.owed, c, SubtractDecimals(SubtractDecimals(MoneyOf(b.earned, c), MoneyOf(b.takenBack, c)), MoneyOf(b.paidOut, c)));
        }
        out.partners.push_back(std::move(b));
    }
    std::stable_sort(out.partners.begin(), out.partners.end(), [](const PartnerBalance& a, const PartnerBalance& b) {
        auto owes = [](const PartnerBalance& p) { for (const auto& [_, sum] : p.owed) if (CentsOrZero(sum) > 0) return true; return false; };
        return owes(a) && !owes(b);
    });
    return R::Succeed(std::move(out));
}

Result<std::vector<WeekNet>> WeeklyFinance(const AdminDatabase& db, int weeks, const std::string& untilIso) {
    using R = Result<std::vector<WeekNet>>;
    const auto sales = db.Sales();
    if (!sales) return R::Failure(sales.error.code, sales.error.message, sales.error.detail);
    const auto reversals = db.Reversals();
    if (!reversals) return R::Failure(reversals.error.code, reversals.error.message, reversals.error.detail);
    const std::string lastWeek = WeekStartOf(untilIso.empty() ? NowIso8601() : untilIso);
    if (lastWeek.empty()) return R::Failure(ErrorCode::InvalidArgument, "The date is YYYY-MM-DD", untilIso);
    int64_t lastDays = 0;
    DaysOf(lastWeek, lastDays);
    std::vector<WeekNet> out;
    for (int i = std::max(1, weeks) - 1; i >= 0; --i) {
        WeekNet w;
        w.weekStart = CivilFromDays(lastDays - 7 * i);
        out.push_back(std::move(w));
    }
    auto bucket = [&out](const std::string& at) -> FinanceTotals* {
        const std::string start = WeekStartOf(at);
        for (WeekNet& w : out) if (w.weekStart == start) return &w.totals;
        return nullptr;
    };
    for (const SaleRecord& s : sales.Value()) if (FinanceTotals* t = bucket(s.at)) AddTotals(*t, s);
    for (const ReversalRecord& v : reversals.Value()) if (FinanceTotals* t = bucket(v.at)) AddTotals(*t, v);
    for (WeekNet& w : out) FinishTotals(w.totals);
    return R::Succeed(std::move(out));
}

Result<std::vector<std::string>> FinancePeriods(const AdminDatabase& db) {
    using R = Result<std::vector<std::string>>;
    const auto sales = db.Sales();
    if (!sales) return R::Failure(sales.error.code, sales.error.message, sales.error.detail);
    const auto reversals = db.Reversals();
    if (!reversals) return R::Failure(reversals.error.code, reversals.error.message, reversals.error.detail);
    std::set<std::string> months;
    for (const SaleRecord& s : sales.Value()) if (const std::string m = MonthOf(s.at); !m.empty()) months.insert(m);
    for (const ReversalRecord& v : reversals.Value()) if (const std::string m = MonthOf(v.at); !m.empty()) months.insert(m);
    return R::Succeed(std::vector<std::string>(months.rbegin(), months.rend()));
}

std::string StatementCsv(const FinanceReport& report) {
    std::string csv = "statement," + CsvField(PeriodName(report.period)) + "\n";
    csv += "currency,sales,paid,reversals,refunded,partner_credits,net\n";
    for (const auto& [currency, net] : report.totals.net) {
        csv += currency + "," + std::to_string(report.totals.sales) + "," + MoneyOf(report.totals.paid, currency) + "," + std::to_string(report.totals.reversals) + "," +
               MoneyOf(report.totals.refunded, currency) + "," + MoneyOf(report.totals.credits, currency) + "," + net + "\n";
    }
    csv += "\napp,currency,sales,paid,refunded,partner_credits,net\n";
    for (const AppFinance& a : report.apps) {
        for (const auto& [currency, net] : a.totals.net) {
            csv += CsvField(a.app) + "," + currency + "," + std::to_string(a.totals.sales) + "," + MoneyOf(a.totals.paid, currency) + "," +
                   MoneyOf(a.totals.refunded, currency) + "," + MoneyOf(a.totals.credits, currency) + "," + net + "\n";
        }
    }
    csv += "\npartner,name,currency,credited_in_period,earned,taken_back,paid_out,owed\n";
    for (const PartnerBalance& p : report.partners) {
        std::vector<std::string> currencies;
        for (const ByCurrency* list : {&p.inPeriod, &p.owed}) {
            for (const auto& [c, _] : *list) if (std::find(currencies.begin(), currencies.end(), c) == currencies.end()) currencies.push_back(c);
        }
        for (const std::string& c : currencies) {
            csv += CsvField(p.affiliate.id) + "," + CsvField(p.affiliate.name) + "," + c + "," + MoneyOf(p.inPeriod, c) + "," + MoneyOf(p.earned, c) + "," +
                   MoneyOf(p.takenBack, c) + "," + MoneyOf(p.paidOut, c) + "," + MoneyOf(p.owed, c) + "\n";
        }
    }
    csv += "\nmonth,currency,sales,paid,refunded,partner_credits,net\n";
    for (const MonthNet& m : report.months) {
        if (m.totals.net.empty()) { csv += m.month + ",,0,0.00,0.00,0.00,0.00\n"; continue; }
        for (const auto& [currency, net] : m.totals.net) {
            csv += m.month + "," + currency + "," + std::to_string(m.totals.sales) + "," + MoneyOf(m.totals.paid, currency) + "," +
                   MoneyOf(m.totals.refunded, currency) + "," + MoneyOf(m.totals.credits, currency) + "," + net + "\n";
        }
    }
    return csv;
}

} // namespace UltraStoreAdmin
