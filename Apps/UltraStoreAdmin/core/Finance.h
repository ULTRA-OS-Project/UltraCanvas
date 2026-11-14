// Apps/UltraStoreAdmin/core/Finance.h
// The merchant's books, from the ledger: what came in, what went back, what
// the partners earned and were paid, what is left - by month, by app, by
// partner, for a period. The reversals (the payment service's refunds and
// chargebacks) and the payouts to the partners are recorded here too, so the
// report is complete; the sales themselves are Affiliates.h's.
//
// Every figure is a decimal string in its currency, to the cent, the way the
// ledger keeps amounts: the report never converts between currencies, it
// lists each. Net to the store = paid - refunded - the partners' credits
// (net of what a reversal took back from them). What a partner is owed is
// all-time: earned - taken back - paid out.
//
// The window's Finance page and `ultrastoreadmin finance` are both views of
// BuildFinanceReport, so they never disagree.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "AdminDatabase.h"
#include "Affiliates.h"

#include <string>
#include <utility>
#include <vector>

namespace UltraStoreAdmin {

// Sums by currency, in the order the currencies first appear: ("EUR", "12.30").
using ByCurrency = std::vector<std::pair<std::string, std::string>>;
void AddMoney(ByCurrency& sums, const std::string& currency, const std::string& decimal);   // adds (a negative decimal subtracts)
std::string MoneyOf(const ByCurrency& sums, const std::string& currency);                   // "0.00" when absent
std::string JoinMoney(const ByCurrency& sums);                                              // "12.30 EUR, 4.00 USD", "0.00" when empty
std::string SubtractDecimals(const std::string& a, const std::string& b);                   // a - b, two places, may be negative

// A reversal into the ledger, with the credits it takes back worked out from
// the sale it reverses - named by the payment service's reference, or by its
// number in the ledger for a sale recorded by hand: the same share of the
// amount reversed that the sale credited, to the cent, never more than the
// sale credited. A reversal of a sale the ledger does not know takes nothing
// back and is listed under no app.
Result<ReversalRecord> RecordReversal(AdminDatabase& db, const std::string& kind, const std::string& reverses,
                                      const std::string& app, const std::string& amount, const std::string& currency,
                                      const std::string& at = {}, const std::string& reference = {}, bool revoked = false);

// The refunds and chargebacks the payment service reports
// (GET /v1/reversals?since=), into the ledger once each: the sequence reached
// is kept in the setting payment.reversals_seq, and a reversal whose
// reference is already there is skipped. The fetch is SyncSales's.
Result<SyncReport> SyncReversals(AdminDatabase& db, const std::string& paymentUrl, const std::string& token, const SalesFetcher& fetch = UltraNetSalesFetcher());

// Money paid out to a partner.
Result<PayoutRecord> RecordPayout(AdminDatabase& db, const std::string& affiliate, const std::string& amount, const std::string& currency,
                                  const std::string& note = {}, const std::string& at = {});

// ----- the report -----

// A period is "" (all time), "YYYY" or "YYYY-MM"; a record is in it when its
// date starts with it.
bool InPeriod(const std::string& isoDate, const std::string& period);
std::string MonthOf(const std::string& isoDate);          // "2026-10"
std::string PeriodName(const std::string& period);        // "October 2026", "2026", "All time"

struct FinanceTotals {
    size_t sales = 0, reversals = 0;
    ByCurrency paid;          // the sales' amounts
    ByCurrency refunded;      // the reversals' amounts
    ByCurrency credits;       // the partners' credits of the sales, less what the reversals took back
    ByCurrency net;           // paid - refunded - credits
};

struct AppFinance {
    std::string app;          // the id; the window names it from its components
    FinanceTotals totals;
};

struct MonthNet {
    std::string month;        // "2026-10"
    FinanceTotals totals;
};

struct PartnerBalance {
    Affiliate affiliate;
    size_t sales = 0;         // in the period, with their codes or their recruits'
    ByCurrency inPeriod;      // credited in the period (own and recruit shares)
    ByCurrency earned;        // all time
    ByCurrency takenBack;     // all time, by the reversals
    ByCurrency paidOut;       // all time
    ByCurrency owed;          // earned - takenBack - paidOut
};

struct FinanceReport {
    std::string period;
    FinanceTotals totals;
    std::vector<AppFinance> apps;          // by net, largest first, in the report's main currency
    std::vector<MonthNet> months;          // the twelve months up to the period's last month with data (or the period's end), oldest first
    std::vector<PartnerBalance> partners;  // every partner, the ones with something owed first
    std::vector<ReversalRecord> reversals; // in the period, newest first
    std::string mainCurrency;              // the currency of most sales in the period, "" without a sale
};
Result<FinanceReport> BuildFinanceReport(const AdminDatabase& db, const std::string& period = {});

// The months that have a sale or a reversal, newest first ("2026-10").
Result<std::vector<std::string>> FinancePeriods(const AdminDatabase& db);

// The report as CSV: the statement by currency, the apps, the partners, the
// months. Decimal points, commas between fields, one header line per block.
std::string StatementCsv(const FinanceReport& report);

} // namespace UltraStoreAdmin
