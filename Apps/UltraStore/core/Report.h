// Apps/UltraStore/core/Report.h
// Reporting a problem with an app to the marketplace service the catalog
// names (market_url; Server/UltraStoreMarket, the store's README, *The
// operator tier*): the store's door into the operator's reports. The
// report names its target, its kind and the text, and the licence as the
// reporter when the app was bought here; the service answers an id and a
// secret, with which the reporter learns the outcome in one line. The
// HTTP call is a function the caller hands in (Purchase.h's HttpCall), so
// the tests use a map.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "Purchase.h"
#include "UltraStoreTypes.h"

#include <string>

namespace UltraStore {

// What the service gave back for a report sent.
struct ReportReceipt {
    std::string id, secret, createdAt;
};

// What became of a report.
struct ReportOutcome {
    std::string id, state;      // open, assigned, resolved, dismissed
    std::string outcome;        // the one line, once resolved or dismissed
    std::string resolvedAt;
    bool Closed() const { return state == "resolved" || state == "dismissed"; }
};

// The kinds the store's form offers, as the service records them.
inline const char* const kReportKinds[] = {"crash", "not-working", "malware", "content", "copyright", "privacy", "other"};

// POST /v1/reports: targetKind "app", "release" or "merchant".
Result<ReportReceipt> SendReport(const std::string& marketUrl, const std::string& targetKind, const std::string& target, const std::string& kind,
                                 const std::string& text, const std::string& reporter, const HttpCall& http);
// GET /v1/reports/{id}/outcome?secret=
Result<ReportOutcome> FetchReportOutcome(const std::string& marketUrl, const std::string& id, const std::string& secret, const HttpCall& http);

} // namespace UltraStore
