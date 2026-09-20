// Apps/UltraFIBU/engine/UltraFIBUBeleg.h
// A document - an outgoing invoice, an incoming receipt, a credit note - and
// the positions it is made of.
//
// **Documents sit beside the journal, not inside it** (proposal §6.4). An
// invoice has positions, a partner, a currency, a file, a status and a payment
// history; a posting has two accounts and an amount. Keeping them apart is what
// lets a document be drafted, corrected and re-priced while unposted, and lets
// the journal stay append-only once it is not.
//
// The relation is one-way and deliberate: **a document produces postings.** It
// is never derived from them. `Buchen()` in the store turns a document into
// journal rows and stamps the document with the fact; from that moment the
// document is as immutable as the postings are, and a change is a Storno -
// which is a *new document* reversing the old one, with its own number and its
// own postings.
//
// Two rounding rules that are easy to get wrong and expensive to get wrong
// late:
//
//   - **Tax is computed per tax rate, not per position.** Ten positions at
//     19 % produce one tax figure taken from their summed net, not ten rounded
//     figures added up. The two differ by cents, and the invoice total must
//     match what the recipient's own system computes from the same summary.
//   - **A discount is distributed, not re-applied.** UltraCanvas::Money's
//     SplitProportionally hands out the remainder by largest remainder, so the
//     positions always add back up to the discounted total.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraFIBUTypes.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace UltraFIBU {

// What kind of document this is. The art decides the sign of the posting, which
// person account it uses, and which side of the UStVA it lands on - so it is
// never inferred from the amount.
enum class BelegArt {
    Ausgangsrechnung,    // we invoice a customer
    Eingangsrechnung,    // a supplier invoices us
    Ausgangsgutschrift,  // we credit a customer (a reversal of revenue)
    Eingangsgutschrift,  // a supplier credits us
    Kassenbeleg,         // cash, no person account
    Sonstiges
};

std::string BelegArtToText(BelegArt art);
bool        BelegArtFromText(const std::string& text, BelegArt& out);
std::string BelegArtLabel(BelegArt art);        // German label for the UI

// True when this kind of document is one we issue (a receivable), false when it
// is one we receive (a payable). Credit notes invert the money but not the
// side: an Ausgangsgutschrift is still a document to a customer.
bool IstAusgangsbeleg(BelegArt art);

// True when settling this document means money **leaving** the bank account.
//
// Deliberately not the same question as IstAusgangsbeleg, and the difference is
// the credit notes: an Ausgangsgutschrift is a document to a customer, so
// IstAusgangsbeleg is true - but paying it out is money leaving, because we are
// refunding them. A bank matcher that used the party side instead of the money
// side would offer every refund against every incoming payment.
//
// Kassenbeleg and Sonstiges are answered as money in, which is what the Eingang
// half of their names implies; neither should reach a bank matcher in the first
// place, because cash does not arrive on a statement.
bool GeldAbgangBeimAusgleich(BelegArt art);

// Where the document stands. `Entwurf` is the only status in which it may still
// be changed; everything else has postings behind it.
enum class BelegStatus {
    Entwurf,             // not posted; editable
    Gebucht,             // posted, nothing paid yet
    TeilweiseBezahlt,
    Bezahlt,
    Storniert            // reversed by a Storno document
};

std::string BelegStatusToText(BelegStatus status);
bool        BelegStatusFromText(const std::string& text, BelegStatus& out);
std::string BelegStatusLabel(BelegStatus status);

// ===== POSITION =====

struct BelegPosition {
    int64_t id = 0;
    int64_t belegId = 0;
    int     position = 1;            // 1-based, the order on the printed invoice

    std::string bezeichnung;
    // Quantity in thousandths, so 0,25 hours and 1,5 kg are exact and no
    // double enters the calculation. 1 000 is one unit.
    int64_t     mengeTausendstel = 1000;
    std::string einheit = "Stk";

    Money       einzelpreis;         // net price per unit
    int         rabattPromille = 0;  // 50 = 5 % off this position

    std::string konto;               // revenue or expense account
    std::string steuerschluessel;
    // The rate as it stood on the Belegdatum, copied here when the document is
    // costed. History, not configuration - see UltraFIBUBuchung.h.
    int         satzPromille = 0;

    Money       netto;               // computed: quantity x price, less discount
    Money       steuer;              // computed per rate across the document
    Money       brutto;

    std::string kostenstelle;
    std::string kostentraeger;

    bool Valid() const {
        return !bezeichnung.empty() && einzelpreis.Valid() && mengeTausendstel != 0;
    }

    // Quantity x unit price, less this position's discount. The only place the
    // per-position net is decided; the tax is not decided here, because it is a
    // per-rate figure across the whole document.
    Money NettoBetrag() const;
};

// ===== BELEG =====

struct Beleg {
    int64_t id = 0;
    int64_t mandantId = 0;
    int64_t geschaeftsjahrId = 0;

    BelegArt    art = BelegArt::Ausgangsrechnung;
    std::string nummer;              // from the Nummernkreis; unique per Mandant
    std::string externeNummer;       // the supplier's own invoice number

    Date datum;                      // Belegdatum: the invoice date
    Date leistungVon;                // Leistungsdatum / -zeitraum (§ 14 Abs. 4 UStG)
    Date leistungBis;
    Date faelligAm;

    // Which of the two § 14 Abs. 4 Nr. 6 facts the dates above state, and
    // whether it is a day or a period. Not cosmetic: "geliefert am" and
    // "geleistet im Zeitraum" are different statements about when the tax
    // arose, and an invoice that names neither is missing a mandatory field.
    Leistungszeitpunkt leistungsart = Leistungszeitpunkt::Leistungsdatum;

    // **True when `einzelpreis` on every position is a GROSS price.**
    //
    // A supplier's receipt states gross - 6,55 EUR including VAT - and typing
    // that into a net field silently overstates the expense by the tax. So the
    // form has a Brutto/Netto switch, and this records which way it stood,
    // because the stored price has to stay the number that was typed: a draft
    // reopened a week later must show what the receipt shows.
    //
    // The costing preserves the typed gross exactly. Net and tax are derived
    // from it per rate group, and the per-line shares are distributed so the
    // lines add back to that gross rather than to a re-multiplied figure.
    bool preiseSindBrutto = false;

    // **The tax as the document itself states it**, when it states one.
    // `steuerVorgegeben` is what decides - deliberately a flag rather than
    // Money::Valid(), because a default-constructed Money IS valid (it is a
    // zero with no currency, so sums can start from one), and reading validity
    // as "a figure was supplied" made every document declare a stated tax of
    // 0,00 and lose its tax entirely.
    //
    // This exists because of a real receipt. Four lines netting to 5,51 EUR at
    // 19 %: the supplier's own invoice says 1,04 EUR tax and 6,55 EUR total,
    // because it taxed each line and summed. Tax on the summed net is 1,0469,
    // which rounds to 1,05 - so recomputing turns a 6,55 EUR receipt into a
    // 6,56 EUR posting. Both roundings are defensible; only one of them is what
    // the supplier charged, and it is the one on the paper.
    //
    // On an incoming document the tax is therefore a fact to be recorded, not a
    // figure to be derived - the input-tax deduction has to match the document,
    // and a cent of drift per receipt is a reconciliation nobody can finish.
    // On an outgoing document it is normally left invalid, because there we are
    // the ones deciding.
    //
    // Only meaningful while the document has ONE tax rate: apportioning a
    // single stated total across several rates would be a guess, so Summieren
    // refuses that case rather than inventing a split.
    bool  steuerVorgegeben = false;
    Money vorgegebeneSteuer;

    int64_t     partnerId = 0;
    // The partner's account and name as they were when the document was
    // written. A customer who moves or is renamed must not retroactively
    // change an invoice that has already been sent.
    std::string partnerKonto;
    std::string partnerName;

    std::string waehrung = "EUR";

    Money netto;
    Money steuer;
    Money brutto;
    Money bezahlt;                   // sum of the payments recorded against it

    BelegStatus status = BelegStatus::Entwurf;

    std::string buchungstext;        // what the journal rows will say
    std::string notiz;

    // The scanned or received original. `dateiHash` is a SHA-256 of the file as
    // it was attached: the GoBD ask that the document be unchanged, and a hash
    // is how that is shown rather than asserted.
    std::string dateiPfad;
    std::string dateiHash;

    // Storno, in both directions, for the same reason the journal stores both.
    int64_t stornoVon = 0;           // the document this one reverses
    int64_t storniertDurch = 0;      // the document that reversed this one

    bool festgeschrieben = false;

    int64_t     erfasstVon = 0;
    std::string erfasstVonName;
    int64_t     erfasstAm = 0;
    int64_t     geaendertAm = 0;
    int64_t     version = 1;         // optimistic locking

    std::vector<BelegPosition> positionen;

    bool Valid() const {
        return datum.Valid() && !positionen.empty() && !nummer.empty();
    }

    bool IstStorno()    const { return stornoVon != 0; }
    bool IstStorniert() const { return storniertDurch != 0; }
    // Only a draft may be changed. Everything else has journal rows behind it.
    bool IstAenderbar() const {
        return status == BelegStatus::Entwurf && !festgeschrieben;
    }
    Money Offen() const;             // brutto - bezahlt, zero when not posted

    // Cost the document: per-position net, then tax **per rate** over the summed
    // net of each rate, then the position tax shares distributed from that
    // figure so the positions add back up to the total. Fills every position's
    // netto/steuer/brutto and the document's three totals.
    //
    // `satzFuer` supplies the rate in permille for a Steuerschlüssel, because
    // the rate lives in a database table this header does not know about. It is
    // called once per distinct key. Returning false for a key means "unknown",
    // and the whole costing fails rather than silently taxing at zero.
    bool Summieren(const std::function<bool(const std::string& schluessel,
                                           int& satzPromille)>& satzFuer);

    // The same, for a document whose positions already carry their satzPromille
    // (one read back from the database, or one being re-checked).
    bool Summieren();
};

// ===== IS THE TAX TREATMENT CONSISTENT WITH ITSELF? =====
//
// This exists because of an invoice this program actually produced: 1.000,00
// net, "zzgl. 19 % USt 190,00", total 1.190,00 - and underneath it, in the
// place § 14 Abs. 4 Nr. 8 UStG reserves for the exemption, the sentence
// "Steuerschuldnerschaft des Leistungsempfängers". It charged the tax and told
// the customer they owed it. Whichever half the reader believed, the other one
// was wrong, and nothing in the program objected.
//
// The cause was one tax key doing two jobs: § 13b at 19 % is what the
// *recipient* of a service books to self-assess German tax, and it had been
// used on an outgoing invoice, where the same rule means zero. Splitting the
// keys fixes that instance. This check is what stops the next one, because the
// contradiction is visible without knowing any tax law: a document cannot both
// charge tax and say no tax is charged.
//
// Each entry is a sentence a bookkeeper can act on. `blockierend` marks the
// ones that must stop the document rather than warn about it.
struct SteuerBefund {
    std::string position;     // which line, empty for a document-wide finding
    std::string schluessel;
    std::string text;
    bool        blockierend = false;
};

// Check a document against the tax keys it uses and the partner it is for.
//
// Pure: no database, no clock. `schluessel` is the set of keys in force, and a
// key the document names but the list does not contain is itself a finding -
// costing a document against a key nobody can describe is how a wrong rate
// gets onto an invoice unremarked.
std::vector<SteuerBefund> PruefeSteuerlicheStimmigkeit(
    const Beleg& beleg, const Partner& partner,
    const std::vector<Steuerschluessel>& schluessel);

// True when any finding blocks. What `Buchen` asks.
bool HatBlockierendenBefund(const std::vector<SteuerBefund>& befunde);

// ===== WHICH TAX KEYS BELONG ON THIS DOCUMENT =====
//
// A bookkeeper should not have to know that a service to a Vienna customer is
// "EURC" and goods to the same customer are "IGL". What decides it is where
// the other party is, whether they are a business, whether they gave a VAT
// number, and whether this is a sale or a purchase - all of which the program
// already knows by the time anyone opens the tax dropdown.
//
// So the dropdown is not the whole key list: it is this. The suggestions come
// first and carry a sentence saying why, the rest stay reachable - because the
// law has exceptions and a program that hides the other keys is a program
// somebody works around - and anything outright wrong is marked as such before
// it is chosen rather than refused afterwards.
struct SteuerschluesselVorschlag {
    Steuerschluessel schluessel;
    // Fits this partner and this kind of document. Listed first.
    bool        passend = false;
    // The one to preselect. At most one entry carries it.
    bool        vorgabe = false;
    // Would produce a blocking finding if used. Still listed, so the reason is
    // visible where the choice is made.
    bool        widerspruch = false;
    // One sentence, in German, for the row under the dropdown.
    std::string begruendung;
};

// The keys for a document of `art` to/from `partner`, suggestions first.
//
// Pure: no database. `alle` is the set of keys in force on `datum`; a key not
// valid then is left out entirely, because offering last year's rate is how
// last year's rate ends up on an invoice.
std::vector<SteuerschluesselVorschlag> SteuerschluesselFuerPartner(
    const Mandant& mandant, const Partner& partner, BelegArt art, const Date& datum,
    const std::vector<Steuerschluessel>& alle);

// The distinct tax keys of a document, in the order they first appear - which
// is the order the postings are generated in, so a journal reads like the
// invoice it came from.
std::vector<std::string> SteuerschluesselDesBelegs(const Beleg& beleg);

} // namespace UltraFIBU
