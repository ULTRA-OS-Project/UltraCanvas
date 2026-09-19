// Apps/UltraFIBU/engine/UltraFIBUUstIdNr.h
// European VAT identification numbers: normalisation, format checking and -
// where the national rule is unambiguous - the check digit.
//
// This is the *offline* half of the job and it is worth being precise about
// what it can and cannot do:
//
//  - It catches typing errors instantly, with no network, which is what makes
//    an invoice form usable.
//  - It proves nothing. The only thing that protects a zero-rated
//    intra-community supply is a **qualified confirmation** from the BZSt
//    (§ 18e UStG), and the data set the BZSt returns is itself the evidence -
//    so it is stored verbatim per query, never reduced to a boolean. The BZSt
//    retired its XML-RPC interface on 30 November 2025 in favour of a REST
//    API; that request lives in the network layer, not here.
//  - Where a member state's rule has variants or was relaxed (the Netherlands
//    issues numbers that no longer satisfy the old modulo-11 rule; Spain and
//    Ireland mix letters and digits by several schemes), this returns
//    FormatOk rather than inventing a verdict. A validator that rejects a
//    valid number is worse than one that admits it cannot tell.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>
#include <vector>

namespace UltraFIBU {

enum class UstIdNrStatus {
    Leer,               // nothing entered
    LandUnbekannt,      // the prefix is not an EU member state
    FormatUngueltig,    // right country, wrong shape
    FormatOk,           // shape is right; no check digit rule is applied for this country
    PruefzifferOk,      // shape and check digit are both right
    PruefzifferFalsch   // shape is right, the check digit is not
};

std::string UstIdNrStatusToText(UstIdNrStatus status);

struct UstIdNrPruefung {
    UstIdNrStatus status = UstIdNrStatus::Leer;
    std::string   normalisiert;   // "DE123456789" - upper case, no spaces or punctuation
    std::string   land;           // "DE", "" when unknown
    std::string   hinweis;        // one German sentence for the UI

    // True when nothing here contradicts the number. Not a confirmation - see
    // the header.
    bool Plausibel() const {
        return status == UstIdNrStatus::FormatOk || status == UstIdNrStatus::PruefzifferOk;
    }
};

// Upper-cases and strips everything that is not a letter or a digit, so
// "de 123.456.789" and "DE123456789" normalise alike.
std::string NormalisiereUstIdNr(const std::string& input);

// The whole offline check.
UstIdNrPruefung PruefeUstIdNr(const std::string& input);

// EU member states by ISO 3166-1 alpha-2, as VAT law uses them - so "EL" for
// Greece, and "XI" for Northern Ireland, which stays inside the EU VAT area
// for goods under the Protocol while "GB" does not.
bool IstEuLand(const std::string& iso2);
const std::vector<std::string>& EuLaender();

// Northern Ireland: an EU VAT area for goods, not a member state.
inline bool IstNordirland(const std::string& iso2) { return iso2 == "XI"; }

} // namespace UltraFIBU
