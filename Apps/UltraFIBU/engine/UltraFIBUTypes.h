// Apps/UltraFIBU/engine/UltraFIBUTypes.h
// The master-data types: the company, its chart of accounts, its tax keys, its
// customers and suppliers, and the people who may post.
//
// Three conventions run through all of them, and each one is a decision the
// design proposal argues for (Docs/Research/UltraFIBUDesignProposal.md):
//
//  - **Amounts are UltraCanvas::Money** - integer minor units, never a double.
//  - **Account numbers are strings.** A Sachkonto's width is a per-company
//    setting (4..8 digits) and leading zeros carry meaning, so "1200" and
//    "01200" are different accounts and neither is an int.
//  - **Tax knowledge is data, not code.** Nothing here branches on a country
//    or a rate; the Steuerschluessel table below holds it, versioned by
//    validity date, so a rate change or the yearly UStVA form change is a new
//    row rather than a new code path.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraFIBUDate.h"
#include "UltraFIBUGeschaeftsjahr.h"

#include "UltraCanvasMoney.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraFIBU {

using UltraCanvas::Money;

// ===== MANDANT (the company being booked) =====

// Whether VAT falls due with the invoice (Soll) or with the payment (Ist,
// § 20 UStG). The default is Soll-Versteuerung; the flag switches the EUeR and
// UStVA projection, which is why both the Belegdatum and the payment date are
// always stored.
enum class Besteuerungsart { Sollversteuerung, Istversteuerung };

std::string BesteuerungsartToText(Besteuerungsart art);
bool        BesteuerungsartFromText(const std::string& text, Besteuerungsart& out);

// How the annual result is determined - which decides whether the reports are
// an EUeR or a Bilanz with GuV.
enum class Gewinnermittlung { EinnahmenUeberschuss, Bilanzierung };

std::string GewinnermittlungToText(Gewinnermittlung art);
bool        GewinnermittlungFromText(const std::string& text, Gewinnermittlung& out);

struct Mandant {
    int64_t     id = 0;
    std::string name;
    std::string rechtsform;          // "GmbH", "UG (haftungsbeschränkt)", "Einzelunternehmen"
    std::string strasse;
    std::string plz;
    std::string ort;
    std::string land = "DE";         // ISO 3166-1 alpha-2
    std::string steuernummer;        // the Finanzamt's number for this company
    std::string ustIdNr;             // own USt-IdNr.
    std::string finanzamt;           // name, for the UStVA cover data
    std::string finanzamtNummer;     // the four-digit ELSTER Finanzamt code
    std::string beraternummer;       // DATEV: assigned by the Kanzlei
    std::string mandantennummer;     // DATEV: assigned by the Kanzlei
    std::string waehrung = "EUR";

    Besteuerungsart  besteuerung   = Besteuerungsart::Sollversteuerung;
    Gewinnermittlung gewinnermittlung = Gewinnermittlung::EinnahmenUeberschuss;
    UstvaRhythmus    ustvaRhythmus = UstvaRhythmus::Vierteljaehrlich;
    bool             dauerfristverlaengerung = false;   // § 46 UStDV
    bool             kleinunternehmer = false;          // § 19 UStG: no VAT is charged
    bool             ossRegistriert   = false;
    bool             iossRegistriert  = false;

    // Contact, for invoice footers and the ELSTER cover data.
    std::string telefon;
    std::string email;
    std::string webseite;
    std::string iban;
    std::string bic;
    std::string bank;

    bool Valid() const { return !name.empty(); }
};

// ===== KONTO (a general-ledger or person account) =====

enum class KontoTyp {
    Aktiv,          // asset
    Passiv,         // liability
    Eigenkapital,   // equity
    Ertrag,         // revenue
    Aufwand,        // expense
    Debitor,        // customer person account
    Kreditor,       // supplier person account
    Statistisch     // memo account, never in a balance
};

std::string KontoTypToText(KontoTyp typ);
bool        KontoTypFromText(const std::string& text, KontoTyp& out);

struct Konto {
    int64_t     id = 0;
    int64_t     mandantId = 0;
    std::string nummer;              // "1200", "8400", "10001" - a string, see the header
    std::string bezeichnung;
    KontoTyp    typ = KontoTyp::Aufwand;
    std::string skr = "SKR03";       // which chart this row came from
    std::string steuerschluessel;    // default tax key for postings on this account
    std::string eurZeile;            // Anlage EUeR line, for the cash-basis report
    std::string bwaPosition;         // BWA position
    // ---- What the DATEV chart of accounts says about this account ----
    // From the Kontenrahmen PDF's own legend. `funktion` is the interesting
    // one: AV and AM mark the Automatikkonten, so it is the authority behind
    // `steuerschluessel` rather than a second, competing statement of it.
    std::string funktion;            // KU/V/M (Zusatz), AV/AM/S/F/R (Haupt), "S/AV"
    std::string abschlusszweck;      // HB | SB | EUeR - which statement it belongs in
    std::string programmverbindung;  // U/G/K: hand-over to the tax programs
    std::string nummerBis;           // set when the row describes a range ("0040-42")

    std::string bilanzPosition;      // balance-sheet classification - carried from the
                                     // first import because the Jahresabschluss is in
                                     // scope (proposal §1.1), and retrofitting it means
                                     // re-classifying every account by hand
    bool        aktiv = true;
    std::string notiz;

    bool Valid() const { return !nummer.empty() && !bezeichnung.empty(); }
};

// ===== WHEN THE SUPPLY HAPPENED =====
//
// § 14 Abs. 4 Nr. 6 UStG makes the time of supply a mandatory invoice field,
// and § 31 Abs. 4 UStDV lets the calendar month stand for it. It is a
// selection rather than a date field alone because the words differ and mean
// different things: goods are *delivered* on a day, a service is *performed*
// over one or a period, and the recipient's input-tax deduction hangs on which.
enum class Leistungszeitpunkt {
    Lieferdatum,        // goods, one day
    Leistungsdatum,     // a service, one day
    Lieferzeitraum,     // goods, over a period
    Leistungszeitraum,  // a service, over a period
    // Deliberately available, deliberately flagged. An invoice for something
    // not yet supplied - an Anzahlungsrechnung - has no time of supply yet, and
    // that is the case this exists for. On an ordinary invoice it is a missing
    // mandatory field, and PruefePflichtangaben says so.
    Keiner
};

std::string LeistungszeitpunktToText(Leistungszeitpunkt art);
bool        LeistungszeitpunktFromText(const std::string& text, Leistungszeitpunkt& out);
std::string LeistungszeitpunktLabel(Leistungszeitpunkt art);   // for the UI
bool        LeistungszeitpunktIstZeitraum(Leistungszeitpunkt art);

// ===== STEUERSCHLUESSEL (how a transaction is taxed) =====

enum class SteuerArt {
    Inland,              // domestic, 19 % / 7 % / 0 %
    IgLieferung,         // intra-community supply of GOODS, zero-rated (§ 4 Nr. 1b UStG)
    IgErwerb,            // intra-community acquisition, taxed here
    // A SERVICE to a business in another member state. The place of supply is
    // the customer's country (§ 3a Abs. 2 UStG), so no German VAT is charged
    // and the customer accounts for it there - "reverse charge". It is a
    // different rule from § 13b below, which is about who owes German tax, and
    // conflating the two produces an invoice that charges 19 % while telling
    // the customer they owe the tax. That invoice is wrong twice.
    EuSonstigeLeistung,
    Drittland,           // third country: export, or an import with no deductible VAT
    ReverseCharge13b,    // § 13b UStG, the recipient owes the GERMAN tax
    Oss,                 // taxed in the destination member state, reported to the BZSt
    Kleinunternehmer,    // § 19 UStG, no VAT charged
    NichtSteuerbar       // place of supply abroad, no German tax
};

// True when a key means "this customer is not charged German VAT", whatever the
// reason. On an outgoing document such a key must carry a rate of zero: an
// invoice that charges tax and also states an exemption contradicts itself, and
// whichever of the two the reader believes, one of them is wrong.
bool IstNullsatzImAusgang(SteuerArt art);

// True when the invoice has to carry the recipient's USt-IdNr. and the note
// naming the rule - § 14a UStG. Zero-rating a cross-border B2B supply without
// the customer's number is not a formality: it is the condition for the
// exemption, and without it the supply is taxable at home.
bool BrauchtUstIdNrDesEmpfaengers(SteuerArt art);

std::string SteuerArtToText(SteuerArt art);
bool        SteuerArtFromText(const std::string& text, SteuerArt& out);

struct Steuerschluessel {
    int64_t     id = 0;
    int64_t     mandantId = 0;
    std::string schluessel;          // short internal key: "USt19", "IGL", "OSS-AT-20"
    std::string bezeichnung;         // German label for the UI
    SteuerArt   art = SteuerArt::Inland;
    int         satzPromille = 0;    // 190 = 19 %, 70 = 7 %, 0 = 0 %, 25 = 2,5 %
    std::string land;                // destination country for OSS / Drittland rows
    bool        vorsteuer = false;   // true: an input-tax key (purchases)
    std::string datevBu;             // the DATEV BU-Schluessel, both directions
    std::string kzBemessung;         // UStVA Kennzahl for the base
    std::string kzSteuer;            // UStVA Kennzahl for the tax (empty when there is none)
    std::string kontoUmsatz;         // revenue/expense account this key posts to
    std::string kontoSteuer;         // VAT account
    Date        gueltigVon;
    Date        gueltigBis;          // invalid => open-ended

    bool Valid() const { return !schluessel.empty(); }
    bool GueltigAm(const Date& date) const {
        if (!date.Valid()) return false;
        if (gueltigVon.Valid() && date < gueltigVon) return false;
        if (gueltigBis.Valid() && date > gueltigBis) return false;
        return true;
    }
    // The tax on a net amount under this key.
    Money SteuerAufNetto(const Money& netto) const { return netto.TaxOnNet(satzPromille); }
    // The tax contained in a gross amount under this key.
    Money SteuerImBrutto(const Money& brutto) const { return brutto.TaxInGross(satzPromille); }
};

// ===== PARTNER (customer / supplier) =====

enum class PartnerTyp { Kunde, Lieferant, Beides };

std::string PartnerTypToText(PartnerTyp typ);
bool        PartnerTypFromText(const std::string& text, PartnerTyp& out);

// What decides the default tax treatment: where they are, and whether they are
// a business. Together with a confirmed USt-IdNr. this is what makes an
// invoice zero-rated - a three-field decision the UI shows as one sentence.
enum class Steuerkategorie {
    Inland,          // German customer or supplier
    EuUnternehmer,   // business in another member state (reverse charge / zero-rated)
    EuPrivat,        // private customer in another member state (OSS territory)
    Drittland        // outside the EU VAT area
};

std::string SteuerkategorieToText(Steuerkategorie kategorie);
bool        SteuerkategorieFromText(const std::string& text, Steuerkategorie& out);

struct Partner {
    int64_t     id = 0;
    int64_t     mandantId = 0;
    PartnerTyp  typ = PartnerTyp::Kunde;
    std::string konto;               // Personenkonto (Debitor/Kreditor), width per Mandant
    std::string name;
    std::string name2;
    std::string anrede;
    std::string kontaktperson;
    std::string abteilung;
    std::string strasse;
    std::string plz;
    std::string ort;
    std::string land = "DE";

    Steuerkategorie steuerkategorie = Steuerkategorie::Inland;
    std::string     ustIdNr;
    std::string     ustIdNrStatus;       // UstIdNrStatusToText of the last offline check
    Date            ustIdNrGeprueftAm;   // when the BZSt was last asked
    std::string     ustIdNrProtokoll;    // the BZSt response, verbatim - this is the evidence
    std::string     steuernummer;

    std::string email;
    std::string telefon;
    std::string webseite;
    std::string iban;
    std::string bic;

    int         zahlungsfristTage = 14;
    int         skontoPromille    = 0;   // 20 = 2 %
    int         skontoTage        = 0;
    std::string sprache = "de";
    std::string notiz;
    bool        aktiv = true;
    int64_t     angelegtAm = 0;          // epoch seconds
    int64_t     geaendertAm = 0;
    int64_t     version = 1;             // optimistic locking (§10.3 of the proposal)

    bool Valid() const { return !name.empty(); }
    bool IstKunde()     const { return typ == PartnerTyp::Kunde || typ == PartnerTyp::Beides; }
    bool IstLieferant() const { return typ == PartnerTyp::Lieferant || typ == PartnerTyp::Beides; }
};

// ===== BENUTZER (who may do what) =====
// Present from the first schema, not from the day the server arrives: GoBD
// attribution cannot be backfilled, so even a single-user installation records
// a real user on every row.
enum class BenutzerRolle {
    Administrator,   // users, number ranges, chart of accounts, Festschreibung
    Buchhalter,      // post, reverse, file returns
    Erfasser,        // create documents and proposals; may not post or file
    Steuerberater,   // read everything, export DATEV, no writes
    NurLesen
};

std::string BenutzerRolleToText(BenutzerRolle rolle);
bool        BenutzerRolleFromText(const std::string& text, BenutzerRolle& out);
std::string BenutzerRolleLabel(BenutzerRolle rolle);    // German label for the UI

// What a role may do. Checked in the store, never only in the UI - in server
// mode the database is reachable without the UI.
enum class Recht {
    StammdatenLesen,
    StammdatenSchreiben,
    BelegErfassen,
    Buchen,
    Festschreiben,
    SteuerMelden,
    DatevExportieren,
    BenutzerVerwalten
};

bool RolleHatRecht(BenutzerRolle rolle, Recht recht);

struct Benutzer {
    int64_t       id = 0;
    std::string   anmeldename;
    std::string   anzeigename;
    std::string   email;
    BenutzerRolle rolle = BenutzerRolle::Buchhalter;
    bool          aktiv = true;
    int64_t       angelegtAm = 0;
    int64_t       letzterLogin = 0;

    bool Valid() const { return !anmeldename.empty(); }
    bool Darf(Recht recht) const { return aktiv && RolleHatRecht(rolle, recht); }
};

// ===== NUMMERNKREIS (document numbering) =====
// Invoice and document numbers must be unique and without unexplained gaps
// (§ 14 UStG). The next value lives in the database and is taken inside the
// posting transaction - never SELECT MAX(...) + 1, which duplicates under two
// writers, and never at the moment an editor opens, so an abandoned draft
// consumes nothing.
struct Nummernkreis {
    int64_t     id = 0;
    int64_t     mandantId = 0;
    std::string kreis;               // "rechnung", "beleg", "gutschrift"
    std::string praefix;             // "R-" or a pattern: {JJJJ}{MM}
    int64_t     naechste = 1;
    int         stellen = 4;         // zero-padded width of the counter
    bool        jaehrlichZuruecksetzen = false;
    int         letztesJahr = 0;     // for the yearly reset

    bool Valid() const { return !kreis.empty(); }
};

// Render a number from a Nummernkreis and a counter value:
// praefix placeholders {JJJJ} (year), {JJ} (two-digit year) and {MM} (month)
// are filled from `datum`, then the zero-padded counter is appended.
std::string FormatNummer(const Nummernkreis& kreis, int64_t wert, const Date& datum);

} // namespace UltraFIBU
