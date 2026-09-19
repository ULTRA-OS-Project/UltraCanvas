// Apps/UltraFIBU/engine/UltraFIBUBank.h
// Bank statements: reading them, and proposing what each line pays.
//
// Three readers produce one shape. **CAMT.053** (ISO 20022 XML) is the one to
// build on - German banks finished replacing MT940 with it in November 2025 -
// but an archive of MT940 files does not stop existing on that date, and some
// banks still only offer CSV. So: CAMT.053 first, MT940 for the archive, and a
// CSV reader whose column mapping is a **data file** rather than code, the same
// decision as `data/DATEV-Buchungsstapel-v700.csv` and for the same reason.
//
// Four properties of the formats are designed for here rather than discovered
// later, because each one produces a plausible-looking wrong bank balance:
//
//  1. **The amount is unsigned and the direction is a separate field.** CAMT
//     has `<Amt>` plus `<CdtDbtInd>` (CRDT/DBIT); MT940 has the amount plus a
//     C/D marker. Reading the amount as signed, or ignoring the marker, gives
//     a statement that looks right and is inverted. Internally `Bankumsatz`
//     carries a **signed** amount - positive is money in - because that is what
//     a Kontoauszug shows and what makes the balance identity below work.
//  2. **CAMT decimals are dots, MT940 decimals are commas**, and neither is the
//     process locale's business. Parsed with `Money::TryParse` and an explicit
//     style, never `std::stod`.
//  3. **The counterparty depends on the direction.** On a credit the other
//     party is the *Debtor*; on a debit it is the *Creditor*. A reader that
//     always takes one of them names the wrong party on half the statement -
//     and that half is silently wrong, because the amounts still add up.
//  4. **A statement checks itself.** It carries its opening balance, its
//     closing balance and its entries, and `opening + sum(entries) == closing`
//     must hold. `Bankauszug::Stimmt()` is that identity, and it is the one
//     assertion that catches a dropped entry, a doubled entry and an inverted
//     sign at once - the bank-statement equivalent of a DATEV round trip.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraFIBUTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraFIBU {

// A bank account of the Mandant, and the G/L account its movements post to.
//
// `konto` is what joins the two worlds: a bank line eventually becomes a
// posting on that account, so it is master data set once rather than something
// chosen per import - choosing it per import is how a company ends up with its
// current account spread over three different G/L accounts.
struct Bankkonto {
    int64_t     id = 0;
    int64_t     mandantId = 0;
    std::string bezeichnung;         // "Geschäftskonto Commerzbank"
    std::string iban;
    std::string bic;
    std::string bank;
    std::string konto;               // the G/L account, e.g. SKR03 1200
    std::string waehrung = "EUR";
    std::string csvProfil;           // which profile a CSV from this bank needs
    bool        aktiv = true;
    Date        letzterImportBis;    // how far the statements have been read
    int64_t     version = 1;

    bool Valid() const { return !bezeichnung.empty() && !konto.empty(); }
};

// Which reader produced a statement. Recorded per import, because "the numbers
// are odd" is answered differently for a CSV a user mapped themselves than for
// a CAMT file the bank generated.
enum class BankFormat { Camt053, Mt940, Csv };

std::string BankFormatToText(BankFormat format);
bool        BankFormatFromText(const std::string& text, BankFormat& out);

// One line of a statement.
//
// `referenz` is the idempotency key and the single most important field here:
// it is what makes importing the same statement twice change nothing, which is
// the most common way a bookkeeping system acquires duplicate entries. CAMT's
// `AcctSvcrRef` is the bank's own reference and is used when present; where a
// file gives none, a reference is derived from the statement and the entry's
// content **including its position in the statement**, because two genuinely
// identical lines on one day are possible and collapsing them would drop one.
struct Bankumsatz {
    int64_t     id = 0;
    int64_t     bankkontoId = 0;

    Date        buchungstag;        // BookgDt - the day the bank booked it
    Date        valuta;             // ValDt - the day it counts for interest
    // Signed: positive is money into the account. The formats store magnitude
    // plus a direction flag; the sign is applied once, here, on the way in.
    Money       betrag;

    std::string gegenIban;
    std::string gegenBic;
    std::string gegenName;
    std::string verwendungszweck;   // all Ustrd lines joined, SEPA tags removed
    std::string endToEndId;         // EREF - what a payer's system called it
    std::string mandatsreferenz;    // MREF, on a direct debit
    std::string glaeubigerId;       // CRED, on a direct debit
    std::string buchungstext;       // the bank's own wording ("SEPA-GUTSCHRIFT")
    std::string referenz;           // the idempotency key, unique per account

    // How many TxDtls the entry carried. 0 or 1 is an ordinary line; more means
    // a Sammelbuchung - one amount on the account covering several payments.
    // The entry stays **one** row, because that is what hit the balance, and
    // splitting it would break the identity in Bankauszug::Stimmt().
    int         teilbuchungen = 0;

    bool Valid() const { return buchungstag.Valid() && betrag.Valid() && !referenz.empty(); }
    bool Eingang() const { return betrag.Minor() > 0; }
};

// One statement: an account, a period, two balances and the entries between
// them.
struct Bankauszug {
    std::string iban;
    std::string bic;
    std::string waehrung = "EUR";
    std::string auszugsnummer;      // the bank's statement number
    Date        von;
    Date        bis;

    Money       anfangssaldo;
    Money       endsaldo;
    bool        saldenGelesen = false;   // false: the file carried no balances

    std::vector<Bankumsatz> umsaetze;

    // The self-check: opening + every entry = closing. A file that carries
    // balances and fails this has been read wrong, and reporting that is worth
    // more than importing the entries and hoping.
    bool Stimmt(Money& outDifferenz) const;
};

// What reading a file found. Problems are collected rather than thrown: the
// first useful thing to do with a bank's file is see what is in it.
struct BankLeseBericht {
    bool        ok = false;
    std::string fehler;

    BankFormat  format = BankFormat::Camt053;
    std::vector<Bankauszug> auszuege;    // CAMT may carry several

    int gelesen       = 0;      // entries seen
    int uebernommen   = 0;      // entries that became a Bankumsatz
    int uebersprungen = 0;      // entries that could not be read
    // Entries the file marks as not yet booked (CAMT `<Sts>PDNG`). They are
    // deliberately **not** imported: a pending entry can still change or
    // disappear, and one that was imported and then vanished is a ledger
    // difference nobody can explain later.
    int vorgemerkt    = 0;

    std::vector<std::string> warnungen;
    std::vector<std::string> fehlerZeilen;

    std::string dateiHash;      // SHA-256, so a re-import is recognisable

    int Anzahl() const {
        int n = 0;
        for (const Bankauszug& a : auszuege) n += static_cast<int>(a.umsaetze.size());
        return n;
    }
};

// ===== THE READERS =====

// CAMT.053 (ISO 20022). Handles camt.053.001.02 and .08, which differ in ways
// this reader does not depend on, and tolerates a namespace prefix on every
// element - tinyxml2 does not strip prefixes, and whether a German bank writes
// one is not predictable.
BankLeseBericht LiesCamt053(const std::string& dateipfad);

// MT940 (SWIFT). Kept for the archive: the format was replaced in November
// 2025, so new files are CAMT, but the years before that are MT940 and they
// still have to be readable.
BankLeseBericht LiesMt940(const std::string& dateipfad);

// How a bank's CSV export is laid out. A data file rather than code, because
// every bank's CSV differs and a new one must not need a new build.
struct CsvBankProfil {
    std::string name;
    char        trenner = ';';
    int         kopfzeilen = 1;
    std::string datumsformat = "TT.MM.JJJJ";
    std::string dezimaltrenner = ",";
    std::string encoding = "cp1252";
    // Column indices, -1 when the profile does not give the column.
    int spalteBuchungstag = -1, spalteValuta = -1, spalteBetrag = -1;
    int spalteWaehrung = -1, spalteGegenName = -1, spalteGegenIban = -1;
    int spalteGegenBic = -1, spalteVerwendungszweck = -1, spalteBuchungstext = -1;
    // Some banks give two amount columns (Soll and Haben) instead of one
    // signed one; others give an amount plus a separate direction column.
    int spalteSoll = -1, spalteHaben = -1, spalteRichtung = -1;
    std::string richtungEingang = "H";

    bool Laden(const std::string& dateipfad, std::string& fehler);
};

// A CSV export read through a profile. The IBAN is not usually in the file, so
// it is supplied by the caller from the bank account being imported into.
BankLeseBericht LiesBankCsv(const std::string& dateipfad, const CsvBankProfil& profil,
                            const std::string& iban, const std::string& waehrung);

// Pick the reader by looking at the file, not at its extension: a bank that
// names a CAMT file ".txt" is not an unusual bank.
BankLeseBericht LiesBankdatei(const std::string& dateipfad, const CsvBankProfil& csvProfil,
                              const std::string& iban, const std::string& waehrung);

// Where the shipped CSV profiles live, honouring ULTRAFIBU_DATA_DIR the same
// way the chart of accounts does.
std::string BankProfilPfad(const std::string& dateiname);

// ===== SEPA REMITTANCE TAGS =====

// German banks pack several fields into one remittance line, as
// `EREF+...MREF+...CRED+...SVWZ+...`. That string in a Verwendungszweck column
// is not a Verwendungszweck: the human-written part is what follows `SVWZ+`.
// Exposed because it is easy to get wrong in a way nobody notices - the text
// still looks full of information.
struct SepaTags {
    std::string endToEndId;      // EREF
    std::string kundenreferenz;  // KREF
    std::string mandatsreferenz; // MREF
    std::string glaeubigerId;    // CRED
    std::string verwendungszweck;// SVWZ, or the whole string when untagged
    std::string abweichenderName;// ABWA
};
SepaTags ZerlegeSepaTags(const std::string& text);

// ===== AUTOMATIC ASSIGNMENT =====
//
// A scoring matcher, not magic, and above all **a proposal**. Nothing here
// posts anything: a wrong automatic posting inside a festgeschriebener
// Zeitraum can only be corrected by a Storno, so the cost of a confident
// mistake is far higher than the cost of one confirmation click.
//
// The direction is a precondition rather than a score. Money arriving can only
// pay an outgoing invoice, money leaving can only pay an incoming one; a
// candidate on the wrong side is not a weak match, it is not a match at all.

enum class ZuordnungGuete {
    Sicher,         // the document number is in the remittance and the amount agrees
    Wahrscheinlich, // the amount agrees and the party is identified
    Moeglich        // enough to show, not enough to preselect
};

std::string ZuordnungGueteToText(ZuordnungGuete guete);

// What the matcher needs to know about a candidate document. A plain struct
// rather than a Beleg so the scoring stays pure and a test can drive it
// without a database.
struct ZuordnungKandidat {
    int64_t     belegId = 0;
    std::string belegnummer;
    std::string externeNummer;   // a supplier's own invoice number
    Date        belegdatum;
    Money       offen;           // what is still unpaid
    Money       brutto;
    bool        geldAbgang = false;  // true: settling it takes money OUT of the
                                 // account - which for a credit note is not the
                                 // same as "we received it"
    int64_t     partnerId = 0;
    std::string partnerName;
    std::string partnerIban;
};

struct Zuordnungsvorschlag {
    int64_t        belegId = 0;
    std::string    belegnummer;
    Money          betrag;          // what would be assigned
    ZuordnungGuete guete = ZuordnungGuete::Moeglich;
    int            punkte = 0;
    // Why, in German, so the user judges the proposal instead of trusting it.
    std::vector<std::string> gruende;
    // True when the bank amount is smaller than the open amount: a part
    // payment, which needs the n:m assignment rather than closing the document.
    bool teilzahlung = false;
};

// Score one bank line against the open documents. Returns only candidates that
// scored above the floor, best first.
//
// There is no "today" parameter and no clock call: every date this compares -
// the document's and the bank line's - comes out of the data, so the same
// inputs always give the same proposals and a test cannot be flaky.
std::vector<Zuordnungsvorschlag> SchlageZuordnungVor(
        const Bankumsatz& umsatz, const std::vector<ZuordnungKandidat>& kandidaten);

// Document numbers survive a trip through a payer's typing: "R-202606001" comes
// back as "R 202606001", "RG202606001" or "Rechnung R202606001". Both sides are
// reduced to upper-case letters and digits before the search, and a number
// shorter than four characters is not searched for at all - "1" appears in
// almost every remittance line and would match everything.
std::string NormalisiereNummer(const std::string& text);

} // namespace UltraFIBU
