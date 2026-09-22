// Apps/UltraFIBU/engine/UltraFIBUOss.h
// One-Stop-Shop: the quarterly return for VAT owed to other member states.
//
// **There is no machine interface.** OSS returns are filed at the BZSt through
// Mein BOP, and the only bulk path that portal offers is a CSV transport file
// uploaded by hand. So the honest shape of this module is: compute, generate,
// hand over - and be exact about the first two, because nothing downstream will
// catch a mistake.
//
// Four things decide the design:
//
//  1. **The rate a customer was charged is what the return must declare**, even
//     if it was wrong. The rate lives in the Steuerschluessel the invoice was
//     booked with; `data/EU-Steuersaetze.csv` is a **check against it**, never
//     a substitute. Silently reporting a different figure from the one on the
//     invoice would put the books, the invoice and the return in three
//     different places.
//  2. **The destination country is the whole point**, and it cannot be
//     guessed. An OSS posting whose tax key names no country stops the return,
//     because "some VAT is owed somewhere in the EU" is not a filing.
//  3. **The § 3c threshold has to be watched before it is crossed.** Below
//     EUR 10 000 of EU-wide B2C distance sales a seller may tax at home; above
//     it, destination rates become compulsory. Crossing it unnoticed means
//     every later invoice carries the wrong VAT, so the warning comes early
//     rather than at the year end.
//  4. **OSS turnover is not German turnover.** It belongs in UStVA Kz 45 -
//     base only, no tax, no effect on the Zahllast - and the two must agree.
//     `PruefeGegenUstva` is that reconciliation, and it is this module's
//     equivalent of a bank statement adding up.
//
// IOSS (consignments up to EUR 150) is the same shape with its own
// registration and a monthly period, so it is a `Verfahren` on these types
// rather than a second copy of them.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraFIBUBuchung.h"
#include "UltraFIBUTypes.h"
#include "UltraFIBUUstva.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace UltraFIBU {

// Which scheme a return belongs to. Same tables, different registration and
// different period.
enum class OssVerfahren {
    Oss,    // goods and services to EU consumers; quarterly
    Ioss    // imported consignments up to EUR 150; monthly
};

std::string OssVerfahrenToText(OssVerfahren v);
bool        OssVerfahrenFromText(const std::string& text, OssVerfahren& out);

// A member state's VAT rate, as that state sets it, for the span it applies to.
//
// **A rate that changes is a new row, never an edit.** The old one keeps its
// span and stops at the day before the new one starts. Overwriting it would
// mean a return already filed no longer reproduces the figures it was filed
// with, and the difference would be invisible: the books, the invoice and the
// return would simply disagree the next time anyone looked.
struct EuSteuersatz {
    int64_t     id = 0;        // 0 for a rate read from the CSV rather than the table
    std::string land;          // ISO 3166-1 alpha-2
    std::string art;           // "standard" | "ermaessigt"
    int         satzPromille = 0;
    Date        gueltigVon, gueltigBis;
    // False means: not checked against an authoritative source, and therefore
    // **not used for comparison**. A guessed rate that reports a correct
    // invoice as wrong is worse than no rate at all.
    bool        geprueft = false;
    std::string quelle;

    bool GueltigAm(const Date& datum) const;
};

class EuSteuersaetze {
public:
    bool Laden(const std::string& dateipfad, std::string& fehler);

    // The same set out of the database rather than the shipped file. The table
    // is the editable copy: the CSV is what a fresh installation starts from.
    void Setze(std::vector<EuSteuersatz> saetze) { saetze_ = std::move(saetze); }
    const std::vector<EuSteuersatz>& Alle() const { return saetze_; }

    size_t Anzahl() const { return saetze_.size(); }
    // The verified standard rate for a country on a date, or false when there
    // is none to compare against. Where more than one row covers the date, the
    // one that started latest wins - so a rate entered without closing its
    // predecessor still resolves to the newer rule rather than to whichever
    // row happened to be read first.
    bool Standardsatz(const std::string& land, const Date& datum, int& outPromille) const;
    bool Kennt(const std::string& land) const;

private:
    std::vector<EuSteuersatz> saetze_;
};

std::string EuSteuersaetzePfad();

// One line of the return: a destination country at one rate, for one period.
struct OssPosten {
    std::string land;
    int         satzPromille = 0;
    Money       bemessung;      // net, as invoiced
    Money       steuer;         // the destination country's VAT, as invoiced
    int         buchungen = 0;
    std::vector<int64_t> buchungIds;

    // What the rate check found. Empty means the rate agreed with the shipped
    // one, or that there was no verified rate to compare with - which are
    // different things, and `satzGeprueft` says which.
    bool        satzGeprueft = false;
    std::string satzHinweis;
};

// Turnover that cannot be declared. Each one stops the return: a figure
// missing from an OSS filing is VAT owed to another country and not paid.
struct OssLuecke {
    std::string steuerschluessel;
    Money       bemessung;
    Money       steuer;
    int         buchungen = 0;
    std::string grund;
};

struct OssBerechnung {
    bool         ok = false;
    std::string  fehler;

    OssVerfahren verfahren = OssVerfahren::Oss;
    int64_t      mandantId = 0;
    int          jahr = 0;
    std::string  zeitraum;      // "Q1".."Q4" for OSS, "01".."12" for IOSS
    Date         von, bis;

    // Per country, per rate - which is exactly how the return is laid out.
    std::vector<OssPosten> posten;

    Money summeBemessung;
    Money summeSteuer;

    std::vector<OssLuecke>   luecken;
    std::vector<std::string> warnungen;

    bool Vollstaendig() const { return luecken.empty(); }
    Money SteuerFuer(const std::string& land) const;
};

// Compute a return from the journal. Pure: no database, no clock.
//
// The destination country comes from the Steuerschluessel's `land`. A key with
// no country produces a gap rather than a guess - an OSS posting that cannot
// name its country cannot be filed, and inventing one would send another
// state's VAT to the wrong place.
OssBerechnung BerechneOss(OssVerfahren verfahren, int64_t mandantId, int jahr,
                          const std::string& zeitraum, const Date& von, const Date& bis,
                          const std::vector<Buchung>& journal,
                          const std::vector<Steuerschluessel>& steuerschluessel,
                          const EuSteuersaetze& saetze);

// Period bounds. OSS is quarterly ("Q1".."Q4"), IOSS monthly ("01".."12").
bool OssZeitraumGrenzen(OssVerfahren verfahren, int jahr, const std::string& zeitraum,
                        Date& outVon, Date& outBis);

// ===== THE § 3c THRESHOLD =====

// Where the seller stands against the EUR 10 000 EU-wide distance-selling
// threshold. Below it, taxing at home is allowed; above it, destination rates
// are compulsory from the transaction that crosses it.
struct SchwellenStand {
    Money   summe;              // EU-wide B2C turnover in the calendar year
    Money   schwelle;           // EUR 10 000 unless the law changes
    bool    ueberschritten = false;
    Date    ueberschrittenAm;   // the day it was crossed, when it was
    // True when the seller is close enough that the next few invoices could
    // cross it. Warning early is the point: crossing it unnoticed means every
    // later invoice carries the wrong VAT.
    bool    nahe = false;
    std::vector<std::string> hinweise;
};

// Track the threshold across a calendar year. `schwelleMinor` is a parameter
// rather than a constant because it is a number in a statute, and statutes
// change; the caller passes what the year's law says.
SchwellenStand PruefeLieferschwelle(int jahr, const std::vector<Buchung>& journal,
                                    const std::vector<Steuerschluessel>& steuerschluessel,
                                    int64_t schwelleMinor = 1000000);

// ===== RECONCILIATION WITH THE UStVA =====

// OSS turnover belongs in UStVA Kz 45, base only. The two are computed from the
// same journal by different paths, so they must agree - and when they do not,
// one of the two returns is wrong and it is worth knowing which before both
// are filed.
struct OssUstvaAbgleich {
    bool  stimmt = false;
    Money ossBemessung;
    Money ustvaKz45;
    Money differenz;
    std::string hinweis;
};

OssUstvaAbgleich PruefeGegenUstva(const OssBerechnung& oss,
                                  const UstvaBerechnung& ustva);

// ===== THE TRANSPORT FILE =====

struct OssDateiErgebnis {
    bool        ok = false;
    std::string fehler;
    std::string datei;
    std::string hash;
    std::vector<std::string> warnungen;
};

// Write the BOP transport CSV.
//
// **The column layout is not published.** The BZSt offers the import function
// but not its specification, so this is a reconstruction - and, like the DATEV
// column definition, it says so in the file it writes rather than hoping. The
// figures inside it come straight from the journal and are checkable; the
// arrangement is what needs confirming against a real BOP export.
//
// Refuses an incomplete return, for the same reason the UStVA writer does:
// a missing line is VAT owed to another member state and not declared.
OssDateiErgebnis SchreibeBopDatei(const OssBerechnung& berechnung,
                                  const std::string& ustIdNr,
                                  const std::string& zielVerzeichnis);

} // namespace UltraFIBU
