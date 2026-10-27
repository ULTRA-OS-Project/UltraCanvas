// Apps/UltraStoreAdmin/core/Affiliates.cpp
// The affiliate scheme (Affiliates.h).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "Affiliates.h"

#include "Plugins/QRCode/UltraCanvasQRCode.h"
#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>

using UltraStore::ErrorCode;

namespace UltraStoreAdmin {

namespace {

// Hundredths of a decimal string; false when it is not one.
bool Cents(const std::string& decimal, int64_t& out) {
    int64_t whole = 0, frac = 0;
    int fracDigits = 0;
    bool point = false, any = false;
    for (char c : decimal) {
        if (c == '.') { if (point) return false; point = true; continue; }
        if (c < '0' || c > '9') return false;
        any = true;
        if (!point) whole = whole * 10 + (c - '0');
        else if (fracDigits < 2) { frac = frac * 10 + (c - '0'); ++fracDigits; }
    }
    if (!any) return false;
    if (fracDigits == 1) frac *= 10;
    out = whole * 100 + frac;
    return true;
}

std::string FromCents(int64_t cents) {
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%lld.%02lld", static_cast<long long>(cents / 100), static_cast<long long>(cents % 100));   // locale-ok: a decimal point, as the catalog writes prices
    return buffer;
}

std::string TrimSlash(std::string s) {
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

std::string Escape(const std::string& text) {
    std::string out;
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

void Add(std::vector<std::pair<std::string, std::string>>& totals, const std::string& currency, const std::string& amount) {
    for (auto& [cur, sum] : totals) if (cur == currency) { sum = AddDecimals(sum, amount); return; }
    totals.emplace_back(currency, AddDecimals("0", amount));
}

} // namespace

std::string AddDecimals(const std::string& a, const std::string& b) {
    int64_t x = 0, y = 0;
    if (!Cents(a, x)) x = 0;
    if (!Cents(b, y)) y = 0;
    return FromCents(x + y);
}

std::string PercentOf(const std::string& amount, int percent) {
    int64_t cents = 0;
    if (!Cents(amount, cents) || percent <= 0) return "0.00";
    return FromCents((cents * percent + 50) / 100);
}

Result<Credits> CreditsFor(const AdminDatabase& db, const std::string& code, const std::string& amount) {
    using R = Result<Credits>;
    Credits out;
    if (UltraStore::NormalizeCode(code).empty()) return R::Succeed(out);
    const auto record = db.GetCode(code);
    if (!record) return record.error.code == ErrorCode::NotFound ? R::Succeed(out) : R::Failure(record.error.code, record.error.message, record.error.detail);
    const CodeRecord& c = record.Value();
    if (!c.enabled || c.code.kind != UltraStore::CodeKind::Affiliate || c.code.affiliate.empty()) return R::Succeed(out);
    const auto affiliate = db.GetAffiliate(c.code.affiliate);
    if (!affiliate) return R::Succeed(out);
    const Affiliate& a = affiliate.Value();
    if (!a.enabled) return R::Succeed(out);
    out.affiliate = a.id;
    out.affiliateCredit = PercentOf(amount, a.sharePercent);
    // One level up and no further: the recruiter's recruiter earns nothing.
    if (!a.recruitedBy.empty()) {
        if (const auto recruiter = db.GetAffiliate(a.recruitedBy); recruiter && recruiter.Value().enabled) {
            out.recruiter = recruiter.Value().id;
            out.recruiterCredit = PercentOf(amount, recruiter.Value().recruitSharePercent);
        }
    }
    return R::Succeed(out);
}

Result<SaleRecord> RecordSale(AdminDatabase& db, const std::string& code, const std::string& app,
                              const std::string& amount, const std::string& currency, const std::string& at) {
    using R = Result<SaleRecord>;
    int64_t cents = 0;
    if (!Cents(amount, cents)) return R::Failure(ErrorCode::InvalidArgument, "The amount is a decimal such as 29.90", amount);
    if (currency.size() != 3) return R::Failure(ErrorCode::InvalidArgument, "The currency is a 3-letter code", currency);
    const auto credits = CreditsFor(db, code, FromCents(cents));
    if (!credits) return R::Failure(credits.error.code, credits.error.message, credits.error.detail);
    SaleRecord sale;
    sale.at = at;
    sale.code = UltraStore::NormalizeCode(code);
    sale.app = app;
    sale.amount = FromCents(cents);
    sale.currency = currency;
    sale.affiliate = credits.Value().affiliate;
    sale.recruiter = credits.Value().recruiter;
    sale.affiliateCredit = credits.Value().affiliateCredit;
    sale.recruiterCredit = credits.Value().recruiterCredit;
    return db.AddSale(sale);
}

Result<std::vector<AffiliateTotals>> AffiliateReport(const AdminDatabase& db) {
    using R = Result<std::vector<AffiliateTotals>>;
    const auto affiliates = db.Affiliates();
    if (!affiliates) return R::Failure(affiliates.error.code, affiliates.error.message, affiliates.error.detail);
    const auto sales = db.Sales();
    if (!sales) return R::Failure(sales.error.code, sales.error.message, sales.error.detail);
    std::vector<AffiliateTotals> out;
    for (const Affiliate& a : affiliates.Value()) {
        AffiliateTotals t;
        t.affiliate = a;
        for (const SaleRecord& s : sales.Value()) {
            if (s.affiliate == a.id) { ++t.ownSales; Add(t.ownCredit, s.currency, s.affiliateCredit); }
            if (s.recruiter == a.id) { ++t.recruitSales; Add(t.recruitCredit, s.currency, s.recruiterCredit); }
        }
        out.push_back(std::move(t));
    }
    return R::Succeed(std::move(out));
}

std::string CodeLink(const std::string& catalogBase, const std::string& code) {
    return TrimSlash(catalogBase) + "/go/" + UltraStore::NormalizeCode(code);
}

std::string JoinLink(const std::string& catalogBase, const std::string& affiliateId) {
    return TrimSlash(catalogBase) + "/join/" + affiliateId;
}

std::string StoreLink(const std::string& code) {
    return "ultrastore://code/" + UltraStore::NormalizeCode(code);
}

Result<void> WriteQrCodeSvg(const std::string& text, const std::string& fileUtf8) {
    const UltraCanvas::QRCodeData data = UltraCanvas::QRCodeUtils::GenerateQRCode(text, UltraCanvas::QRErrorCorrection::Medium);
    if (!data.valid) return Result<void>::Failure(ErrorCode::Internal, "The QR code could not be made", data.errorMessage);
    // Drawn here rather than through the plug-in's exporter, so the file
    // is the same on every platform: one path of all the dark modules, a
    // four-module quiet zone, 10 px a module.
    const int quiet = 4, module = 10;
    const int side = (data.size + 2 * quiet) * module;
    std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + std::to_string(side) + "\" height=\"" + std::to_string(side) +
                      "\" viewBox=\"0 0 " + std::to_string(side) + " " + std::to_string(side) + "\" shape-rendering=\"crispEdges\">\n"
                      "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>\n<path fill=\"#000000\" d=\"";
    for (int y = 0; y < data.size; ++y) {
        for (int x = 0; x < data.size; ++x) {
            if (!data.IsDark(x, y)) continue;
            svg += "M" + std::to_string((x + quiet) * module) + " " + std::to_string((y + quiet) * module) + "h" + std::to_string(module) +
                   "v" + std::to_string(module) + "h-" + std::to_string(module) + "z";
        }
    }
    svg += "\"/>\n</svg>\n";
    std::ofstream out(UltraCanvas::PathFromUtf8(fileUtf8), std::ios::binary | std::ios::trunc);
    if (!out) return Result<void>::Failure(ErrorCode::IoError, "Cannot write " + fileUtf8);
    out << svg;
    return out ? Result<void>::Succeed() : Result<void>::Failure(ErrorCode::IoError, "Cannot write " + fileUtf8);
}

std::string CodeLandingPage(const UltraStore::PromoCode& code, const std::vector<UltraStore::Component>& apps, const std::string& catalogBase) {
    std::string what;
    if (code.discountPercent > 0) what = std::to_string(code.discountPercent) + "% off";
    else if (!code.discountAmount.empty()) what = code.discountAmount + (code.currency.empty() ? "" : " " + code.currency) + " off";
    std::string names;
    for (const UltraStore::Component& app : apps) {
        if (!code.AppliesTo(app.id)) continue;
        names += "<li>" + Escape(app.name) + "</li>";
    }
    const std::string store = StoreLink(code.code);
    std::string page =
        "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
        "<title>UltraStore code " + Escape(code.code) + "</title>\n"
        "<style>body{font-family:sans-serif;max-width:36em;margin:3em auto;padding:0 1em;color:#1f2937}code{font-size:1.4em;background:#f3f4f6;padding:.2em .5em;border-radius:.3em}"
        "a.button{display:inline-block;background:#2563eb;color:#fff;padding:.6em 1.2em;border-radius:.4em;text-decoration:none}</style>\n</head>\n<body>\n"
        "<h1>Your UltraStore code</h1>\n<p><code>" + Escape(code.code) + "</code></p>\n";
    if (!what.empty()) page += "<p>" + Escape(what) + (names.empty() ? " on every app." : " on:</p>\n<ul>" + names + "</ul>\n<p>");
    else if (!names.empty()) page += "<p>A partner code for:</p>\n<ul>" + names + "</ul>\n<p>";
    else page += "<p>A partner code.";
    if (!code.validUntil.empty()) page += " Valid until " + Escape(code.validUntil) + ".";
    page += "</p>\n<p><a class=\"button\" href=\"" + Escape(store) + "\">Open in UltraStore</a></p>\n"
            "<p>On a computer with UltraStore, the button opens the store with the code. Otherwise enter the code on the store's Discover page: "
            "the prices show what it takes off for 30 days, and the partner is credited when you buy.</p>\n"
            "<p><small>" + Escape(TrimSlash(catalogBase)) + "</small></p>\n</body>\n</html>\n";
    return page;
}

} // namespace UltraStoreAdmin
