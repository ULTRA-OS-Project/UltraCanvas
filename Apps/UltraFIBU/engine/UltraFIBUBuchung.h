// Apps/UltraFIBU/engine/UltraFIBUBuchung.h
// The journal: one posting, and the hash chain that makes the journal
// tamper-evident.
//
// **The row is DATEV-shaped on purpose** (proposal §6.4). A posting is
// `umsatz` (always positive) plus a Soll/Haben flag, a `konto`, a `gegenkonto`
// and a `buSchluessel` - not a list of debit and credit legs. That is the shape
// the Buchungsstapel exports, the shape the Kanzlei reads, and the shape a
// German bookkeeper types. Every other view can be derived from it; the reverse
// is not true.
//
// One consequence has to be understood before reading further: in that shape
// **the tax is not a third row**. When a posting carries a Steuerschlüssel, one
// of its two accounts is net and the other is gross, and the difference goes to
// a tax account that is never typed. DATEV calls this an automatische
// Steuerbuchung. This file therefore records, beside the DATEV fields, what the
// automatic posting actually was - `netto`, `steuer`, `steuerkonto`,
// `satzPromille` and which side was the net one. Three reasons:
//
//   1. A Summen- und Saldenliste has to show the tax account, and deriving it
//      by re-reading the Steuerschlüssel table at report time gives the wrong
//      answer the moment a rate changes.
//   2. The rate that applied on the Belegdatum is history, not configuration.
//      Storing `satzPromille` with the row is what keeps a 2026 return
//      reproducible in 2031.
//   3. Rounding happened once, when the posting was made. Re-deriving it later
//      can differ by a cent, and a ledger that disagrees with itself by a cent
//      is worse than one that is wrong consistently.
//
// **Postings are never updated.** There is no SaveBuchung that takes an
// existing id: a correction is a Storno row plus a new posting, which is what
// the GoBD require (proposal §8.1) and what the store enforces rather than the
// UI.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraFIBUTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraFIBU {

// Which side of the posting `konto` stands on. The flag always refers to
// `konto`; `gegenkonto` is by definition the other one. This is DATEV's
// Soll-/Haben-Kennzeichen and it is why `umsatz` needs no sign.
enum class SollHaben { Soll, Haben };

std::string SollHabenToText(SollHaben sh);        // "S" | "H"
bool        SollHabenFromText(const std::string& text, SollHaben& out);
SollHaben   SollHabenUmgekehrt(SollHaben sh);     // the reversed side, for a Storno

// Which of the two accounts is the net one when a Steuerschlüssel is set - the
// account DATEV would call the Automatikkonto. `Keine` means the posting is
// tax-free and `steuer` is zero.
enum class SteuerSeite { Keine, Konto, Gegenkonto };

std::string SteuerSeiteToText(SteuerSeite seite);
bool        SteuerSeiteFromText(const std::string& text, SteuerSeite& out);

struct Buchung {
    int64_t id = 0;
    int64_t mandantId = 0;
    int64_t geschaeftsjahrId = 0;

    // Both calendars, always (proposal §6.3). `periode` is the fiscal period
    // the Geschäftsjahr assigns; the UStVA period is derived from `belegdatum`
    // alone and is deliberately not stored, because it would then be able to
    // disagree with the date.
    int  periode = 0;
    Date belegdatum;

    // The document this posting came from, 0 for a free-standing entry.
    int64_t     belegId = 0;
    std::string belegfeld1;        // the document number, DATEV's Belegfeld 1
    std::string belegfeld2;        // second reference: order number, dunning level

    // ---- The DATEV row ----
    Money       umsatz;            // always positive; gross when a tax key is set
    SollHaben   sollHaben = SollHaben::Soll;
    std::string konto;
    std::string gegenkonto;
    std::string buSchluessel;      // the DATEV BU-Schlüssel, exported verbatim

    // ---- What the automatic tax posting was ----
    std::string steuerschluessel;  // our key; empty for a tax-free posting
    SteuerSeite steuerSeite = SteuerSeite::Keine;
    int         satzPromille = 0;  // the rate as it stood on belegdatum
    Money       netto;             // umsatz minus steuer
    Money       steuer;
    std::string steuerkonto;

    std::string buchungstext;
    std::string kost1;             // Kostenstelle
    std::string kost2;             // Kostenträger
    std::string waehrung = "EUR";

    // ---- Storno ----
    // `stornoVon` is set on the reversing row and points at the row it
    // reverses; `storniertDurch` is set on the reversed row and points back.
    // Both directions are stored because both questions get asked: "what does
    // this cancel" in a journal, and "is this still valid" in a report.
    int64_t stornoVon = 0;
    int64_t storniertDurch = 0;

    bool festgeschrieben = false;

    // ---- Attribution (GoBD) ----
    int64_t     erfasstVon = 0;
    std::string erfasstVonName;    // the login name as it was, not a join
    int64_t     erfasstAm = 0;     // epoch seconds

    // ---- The hash chain ----
    // `laufendeNummer` is the position in the Mandant's chain, allocated in the
    // same transaction as the row. `hash` = H(prevHash ‖ Kanonisch(*this)).
    int64_t     laufendeNummer = 0;
    std::string prevHash;
    std::string hash;

    bool Valid() const {
        return belegdatum.Valid() && !konto.empty() && !gegenkonto.empty() &&
               konto != gegenkonto && umsatz.Valid() && !umsatz.IsNegative();
    }

    bool IstStorno()  const { return stornoVon != 0; }
    bool IstStorniert() const { return storniertDurch != 0; }

    // The account that is net when a tax key is set, and the one that is gross.
    // Empty strings when the posting is tax-free.
    std::string NettoKonto()  const;
    std::string BruttoKonto() const;
};

// The byte string that is hashed. Length-prefixed fields ("12:Buchungstext;"),
// so no value can imitate a separator and no two different rows can canonicalise
// the same way - a Buchungstext containing a semicolon is exactly the case that
// breaks a naive join.
//
// Deliberately *not* included: `hash`, `prevHash`, `laufendeNummer` (the chain
// itself), `storniertDurch` (set later, on a row that is already sealed) and
// `festgeschrieben` (a date-driven marker that moves after the fact). Everything
// that describes the money, the accounts, the date and the author is included.
std::string KanonischeForm(const Buchung& buchung);

// The chain step: hex SHA-256 over prevHash ‖ KanonischeForm(buchung). An empty
// prevHash is the genesis case and is hashed as an empty string, so the first
// row of a Mandant is chained exactly like every other one.
std::string BerechneHash(const std::string& prevHash, const Buchung& buchung);

// What a chain check found. `ok` with an empty `fehler` means every row from
// the first to the last hashes to what it stores, and each one carries its
// predecessor's hash.
struct HashKettenPruefung {
    bool        ok = true;
    int64_t     geprueft = 0;        // rows examined
    int64_t     ersteFehlerhafteId = 0;
    int64_t     ersteFehlerhafteNummer = 0;
    std::string fehler;              // one German sentence, empty when ok
};

} // namespace UltraFIBU
