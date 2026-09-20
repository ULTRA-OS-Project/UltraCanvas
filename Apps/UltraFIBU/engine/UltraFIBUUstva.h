// Apps/UltraFIBU/engine/UltraFIBUUstva.h
// The Umsatzsteuer-Voranmeldung: computing it from the journal, and handing it
// to ELSTER.
//
// This is the first thing in UltraFIBU whose output goes to a tax authority, so
// it is built around one idea: **a figure nobody can trace is worse than no
// figure**. Three consequences run through the whole file.
//
//  1. **The Kennzahl mapping is data, keyed by year**
//     (`data/UStVA-Kennzahlen-2026.csv`). The BMF reissues the form annually -
//     Kz 43 appeared in 2026 - so a mapping in C++ would mean a new program
//     every December. It is also the only honest place to record which
//     Kennzahlen have been *verified* and which have not.
//  2. **An unmapped amount stops the return.** If a posting carries a
//     Steuerschluessel with no Kennzahl, or one whose Kennzahl is not yet
//     verified, the amount cannot go anywhere - and silently leaving it out is
//     an under-declaration of turnover. So the computation reports it, names
//     the key and the amount, and `SchreibeElsterXml` refuses to write.
//     Refusing is cheap; an incorrect return is not.
//  3. **The return checks itself against the ledger.** Every Kennzahl carries
//     the postings behind it, and two identities are verified: the tax booked
//     in the journal must match the tax the rate implies on the declared base
//     (to the cent, allowing only rounding), and Kz 83 must equal the declared
//     tax minus the declared input tax. Either failing means the return and
//     the books disagree, which is exactly the thing that must never be
//     discovered by the Finanzamt first.
//
// **ERiC is not in this repository and never will be.** Its distribution
// agreement does not permit it. The engine therefore has two transports and the
// one that always works needs nothing: `ElsterXmlFileTransport` writes the XML
// for manual upload in Mein ELSTER. The ERiC transport is an interface with a
// soft-failing stub until somebody supplies the SDK.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraFIBUBuchung.h"
#include "UltraFIBUTypes.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace UltraFIBU {

// How a Kennzahl enters the return, and therefore how it enters Kz 83.
enum class KennzahlArt {
    Bemessung,   // net base; ELSTER derives the tax from the rate
    Steuer,      // a tax amount declared outright (+)
    Vorsteuer,   // deductible input tax (-)
    Frei,        // exempt / non-taxable turnover; no effect on Kz 83
    Berechnet    // a result line, never summed from the journal
};

std::string KennzahlArtToText(KennzahlArt art);
bool        KennzahlArtFromText(const std::string& text, KennzahlArt& out);

// One line of the form.
struct UstvaKennzahl {
    std::string code;             // "81"
    KennzahlArt art = KennzahlArt::Bemessung;
    int         satzPromille = 0; // only meaningful for Bemessung
    // **Whether this Kennzahl has been checked against the BMF Vordruckmuster.**
    // An unverified one is documentation, not a calculation path: it records
    // that the case exists and is not yet answered. Money landing on one stops
    // the return rather than riding out on a guess.
    bool        geprueft = false;
    std::string bezeichnung;
};

// The whole form for one year, read from data.
class UstvaMapping {
public:
    bool Laden(const std::string& dateipfad, std::string& fehler);

    const std::vector<UstvaKennzahl>& Kennzahlen() const { return kennzahlen_; }
    bool Finde(const std::string& code, UstvaKennzahl& out) const;
    size_t Anzahl() const { return kennzahlen_.size(); }

private:
    std::vector<UstvaKennzahl> kennzahlen_;
};

// Where the shipped mapping for a year lives. The year is part of the file
// name because two years are two different forms, and a return for a closed
// period must still be reproducible next year.
std::string UstvaMappingPfad(int jahr);

// What one Kennzahl came to, and what it was made of.
struct KennzahlBetrag {
    std::string code;
    KennzahlArt art = KennzahlArt::Bemessung;
    Money       betrag;
    // The tax actually booked in the journal for this Kennzahl's postings. For
    // a Bemessung line this is what the rate should reproduce; a difference
    // beyond rounding means the books and the form disagree.
    Money       steuerGebucht;
    int         buchungen = 0;
    // The journal rows behind the figure. The BZSt's and the Finanzamt's
    // questions arrive months later, and "where does this number come from"
    // has to be answerable then.
    std::vector<int64_t> buchungIds;
};

// An amount that has nowhere to go. Each one is a reason the return cannot be
// filed, not a rounding difference to shrug at.
struct UstvaLuecke {
    std::string steuerschluessel;
    std::string bezeichnung;
    Money       netto;
    Money       steuer;
    int         buchungen = 0;
    std::string grund;   // no Kennzahl at all, or one that is not verified
};

struct UstvaBerechnung {
    bool        ok = false;
    std::string fehler;

    int64_t     mandantId = 0;
    int         jahr = 0;
    // "01".."12" for a monthly return, "41".."44" for a quarter - the coding
    // ELSTER itself uses, so nothing has to be translated at the last moment.
    std::string zeitraum;
    Date        von, bis;

    std::map<std::string, KennzahlBetrag> kennzahlen;

    // Kz 83: declared tax minus declared input tax. Positive is a payment due,
    // negative is a refund.
    Money       zahllast;
    Money       summeSteuer;
    Money       summeVorsteuer;

    // **Everything that could not be declared.** Not empty means the return is
    // incomplete, and `SchreibeElsterXml` will refuse.
    std::vector<UstvaLuecke> luecken;

    std::vector<std::string> warnungen;
    // Where the tax booked in the journal differs from what the declared base
    // and rate imply. Rounding of a cent or two is normal on many postings; a
    // real difference means a mis-posting or a wrong rate in the books.
    std::vector<std::string> abweichungen;

    bool Vollstaendig() const { return luecken.empty(); }
    Money Betrag(const std::string& code) const;
};

// Compute a return from the journal.
//
// A pure function over postings and tax keys: no database, no clock, no
// locale. The same period computed twice gives the same figures, which is what
// makes a return reproducible a year later when somebody asks about it.
UstvaBerechnung BerechneUstva(int64_t mandantId, int jahr, const std::string& zeitraum,
                              const Date& von, const Date& bis,
                              const std::vector<Buchung>& journal,
                              const std::vector<Steuerschluessel>& steuerschluessel,
                              const UstvaMapping& mapping);

// The figures as filed, as a plain JSON object of Kennzahl to minor units.
//
// Stored beside the submission rather than recomputed later: the journal may
// legitimately have moved on by the time somebody asks, and a recomputation is
// not evidence of what was sent. A human with a text editor can read this
// against the paper form, which is the point of keeping it plain.
std::string KennzahlenJson(const UstvaBerechnung& berechnung);

// The period codes ELSTER uses, from a fiscal calendar the rest of the engine
// already knows: "01".."12" monthly, "41".."44" quarterly.
std::string UstvaZeitraumCode(int monatOderQuartal, bool vierteljaehrlich);
bool        UstvaZeitraumGrenzen(int jahr, const std::string& zeitraum,
                                 Date& outVon, Date& outBis);

// ===== ELSTER =====

// What a submission needs beyond the figures. None of it is invented here: the
// Steuernummer and the Finanzamt code come from the Mandant, and the
// Hersteller-ID comes from a registration this project does not have yet.
struct ElsterKopf {
    std::string steuernummer;        // in the ELSTER 13-digit form
    std::string finanzamtNummer;
    std::string name, strasse, plz, ort;
    std::string herstellerId;        // assigned on manufacturer registration
    std::string produktName = "UltraFIBU";
    std::string produktVersion;
    // **A test submission must be marked as one.** Sending a test return
    // without the Testmerker files it for real; sending a real one with the
    // Testmerker set means it was never filed. Both are bad and neither is
    // obvious afterwards, so this is explicit and has no default that files.
    bool        echtfall = false;
    Date        erstellt;
    bool        berichtigt = false;  // Kz 10: a corrected return
};

struct ElsterErgebnis {
    bool        ok = false;
    std::string fehler;
    std::string datei;
    std::string xmlHash;             // SHA-256 of exactly what was written
    std::vector<std::string> warnungen;
};

// Write the UStVA as ELSTER XML for manual upload in Mein ELSTER.
//
// **Refuses when the computation is incomplete.** A return missing a turnover
// because its Steuerschluessel has no verified Kennzahl is not a return with a
// small gap; it is an under-declaration, and the file would look perfectly
// valid.
ElsterErgebnis SchreibeUstvaXml(const UstvaBerechnung& berechnung,
                                const ElsterKopf& kopf,
                                const std::string& zielVerzeichnis);

// How a return reaches the tax administration. Two implementations, and the
// one that always works needs nothing installed.
class IElsterTransport {
public:
    virtual ~IElsterTransport() = default;
    virtual std::string Name() const = 0;
    // True when this transport can actually be used right now. The ERiC one is
    // false until somebody supplies the SDK, and saying so plainly is the
    // whole point.
    virtual bool Verfuegbar(std::string& warum) const = 0;
    virtual ElsterErgebnis Senden(const UstvaBerechnung& berechnung,
                                  const ElsterKopf& kopf,
                                  const std::string& zielVerzeichnis) = 0;
};

// Always built: writes the XML and stops. The user uploads it.
std::unique_ptr<IElsterTransport> ElsterDateiTransport();

// ERiC, when it is present. Never vendored: its distribution agreement does not
// permit this repository to carry it, so the transport looks for the shared
// library at a configured path and reports exactly what is missing when it is
// not there. Until the SDK is obtained and every signature checked against the
// official handbook, this reports unavailable rather than guessing at an API.
std::unique_ptr<IElsterTransport> ElsterEricTransport(const std::string& ericVerzeichnis);

} // namespace UltraFIBU
