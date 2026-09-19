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

// The distinct tax keys of a document, in the order they first appear - which
// is the order the postings are generated in, so a journal reads like the
// invoice it came from.
std::vector<std::string> SteuerschluesselDesBelegs(const Beleg& beleg);

} // namespace UltraFIBU
