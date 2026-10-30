// Apps/UltraStoreAdmin/core/Affiliates.h
// The affiliate scheme on the admin database: a partner's codes in the
// catalog, the links and QR codes that carry them, and what a sale credits.
//
// Two tiers and no more. A sale made with an affiliate's code credits that
// affiliate their share of the amount paid; when the affiliate was recruited
// by another, the recruiter is credited their recruit share of the same
// amount - once, one level up. The recruiter's own recruiter gets nothing:
// this is a referral bonus for bringing a partner in, not a pyramid.
//
// A link carries the code so a buyer who scans a partner's QR code lands
// with it: https://<catalog base>/go/<CODE> is a page the publish folder
// carries (a line about the code, and the ultrastore://code/<CODE> link that
// opens the store with it); a future affiliate scans
// https://<catalog base>/join/<affiliate> and names the recruiter when they
// are signed up. The redemption itself - the purchase, the limit on uses,
// the payout - is the payment service's; the admin records sales by hand
// until it exists (RecordSale), so the report and the payouts work from
// day one.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "AdminDatabase.h"

#include <string>
#include <vector>

namespace UltraStoreAdmin {

// What a sale credits, by the rule above. The code may be unknown or
// disabled: then nobody is credited and both ids are empty.
struct Credits {
    std::string affiliate, recruiter;
    std::string affiliateCredit = "0.00", recruiterCredit = "0.00";
};
Result<Credits> CreditsFor(const AdminDatabase& db, const std::string& code, const std::string& amount);

// A sale into the ledger with its credits worked out.
Result<SaleRecord> RecordSale(AdminDatabase& db, const std::string& code, const std::string& app,
                              const std::string& amount, const std::string& currency, const std::string& at = {});

// Per affiliate, what the ledger owes: the sales made with their codes and
// their credit, the sales of their recruits and that credit, by currency.
struct AffiliateTotals {
    Affiliate affiliate;
    size_t ownSales = 0, recruitSales = 0;
    std::vector<std::pair<std::string, std::string>> ownCredit;       // currency -> decimal
    std::vector<std::pair<std::string, std::string>> recruitCredit;
};
Result<std::vector<AffiliateTotals>> AffiliateReport(const AdminDatabase& db);

// The decimal sum of two decimals, two places ("12.30" + "0.7" = "13.00").
std::string AddDecimals(const std::string& a, const std::string& b);
// percent of a decimal, to the cent, half up ("29.90" at 20 -> "5.98").
std::string PercentOf(const std::string& amount, int percent);

// The links. The catalog base is the setting catalog.base_url.
std::string CodeLink(const std::string& catalogBase, const std::string& code);         // https://.../go/<CODE>
std::string JoinLink(const std::string& catalogBase, const std::string& affiliateId);  // https://.../join/<id>
std::string StoreLink(const std::string& code);                                       // ultrastore://code/<CODE>

// The QR code of a text as an SVG file (UTF-8 path).
Result<void> WriteQrCodeSvg(const std::string& text, const std::string& fileUtf8);

// The landing page of a code, for the publish folder's go/<CODE>/index.html:
// what the code gives, the apps, and the link that opens the store with it.
std::string CodeLandingPage(const UltraStore::PromoCode& code, const std::vector<UltraStore::Component>& apps, const std::string& catalogBase);

} // namespace UltraStoreAdmin
