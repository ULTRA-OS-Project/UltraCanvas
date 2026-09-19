// Apps/UltraFIBU/engine/UltraFIBUBeleg.cpp
// Enumeration text and the costing of a document. See the header for the two
// rounding rules this file exists to enforce.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUBeleg.h"

namespace UltraFIBU {

// ===== ENUMERATION TEXT =====

std::string BelegArtToText(BelegArt art) {
    switch (art) {
        case BelegArt::Ausgangsrechnung:   return "ausgangsrechnung";
        case BelegArt::Eingangsrechnung:   return "eingangsrechnung";
        case BelegArt::Ausgangsgutschrift: return "ausgangsgutschrift";
        case BelegArt::Eingangsgutschrift: return "eingangsgutschrift";
        case BelegArt::Kassenbeleg:        return "kassenbeleg";
        case BelegArt::Sonstiges:          break;
    }
    return "sonstiges";
}

bool BelegArtFromText(const std::string& text, BelegArt& out) {
    if (text == "ausgangsrechnung")   { out = BelegArt::Ausgangsrechnung;   return true; }
    if (text == "eingangsrechnung")   { out = BelegArt::Eingangsrechnung;   return true; }
    if (text == "ausgangsgutschrift") { out = BelegArt::Ausgangsgutschrift; return true; }
    if (text == "eingangsgutschrift") { out = BelegArt::Eingangsgutschrift; return true; }
    if (text == "kassenbeleg")        { out = BelegArt::Kassenbeleg;        return true; }
    if (text == "sonstiges")          { out = BelegArt::Sonstiges;          return true; }
    return false;
}

std::string BelegArtLabel(BelegArt art) {
    switch (art) {
        case BelegArt::Ausgangsrechnung:   return "Ausgangsrechnung";
        case BelegArt::Eingangsrechnung:   return "Eingangsrechnung";
        case BelegArt::Ausgangsgutschrift: return "Gutschrift an Kunden";
        case BelegArt::Eingangsgutschrift: return "Gutschrift vom Lieferanten";
        case BelegArt::Kassenbeleg:        return "Kassenbeleg";
        case BelegArt::Sonstiges:          break;
    }
    return "Sonstiger Beleg";
}

bool IstAusgangsbeleg(BelegArt art) {
    return art == BelegArt::Ausgangsrechnung || art == BelegArt::Ausgangsgutschrift;
}

std::string BelegStatusToText(BelegStatus status) {
    switch (status) {
        case BelegStatus::Entwurf:          return "entwurf";
        case BelegStatus::Gebucht:          return "gebucht";
        case BelegStatus::TeilweiseBezahlt: return "teilweise_bezahlt";
        case BelegStatus::Bezahlt:          return "bezahlt";
        case BelegStatus::Storniert:        break;
    }
    return "storniert";
}

bool BelegStatusFromText(const std::string& text, BelegStatus& out) {
    if (text == "entwurf")           { out = BelegStatus::Entwurf;          return true; }
    if (text == "gebucht")           { out = BelegStatus::Gebucht;          return true; }
    if (text == "teilweise_bezahlt") { out = BelegStatus::TeilweiseBezahlt; return true; }
    if (text == "bezahlt")           { out = BelegStatus::Bezahlt;          return true; }
    if (text == "storniert")         { out = BelegStatus::Storniert;        return true; }
    return false;
}

std::string BelegStatusLabel(BelegStatus status) {
    switch (status) {
        case BelegStatus::Entwurf:          return "Entwurf";
        case BelegStatus::Gebucht:          return "Offen";
        case BelegStatus::TeilweiseBezahlt: return "Teilweise bezahlt";
        case BelegStatus::Bezahlt:          return "Bezahlt";
        case BelegStatus::Storniert:        break;
    }
    return "Storniert";
}

// ===== POSITION =====

Money BelegPosition::NettoBetrag() const {
    if (!einzelpreis.Valid()) return Money::Invalid(einzelpreis.Currency());

    // Quantity is in thousandths: price x menge / 1000, rounded once,
    // commercially. ScaledBy does the multiply in 128 bits, so a large
    // quantity at a large price cannot overflow on the way.
    Money betrag = einzelpreis.ScaledBy(mengeTausendstel, 1000);
    if (!betrag.Valid()) return betrag;

    if (rabattPromille > 0) {
        // The discount is taken off the line, not added to a separate one:
        // (1000 - rabatt) / 1000, again a single rounding.
        betrag = betrag.ScaledBy(1000 - rabattPromille, 1000);
    }
    return betrag;
}

// ===== BELEG =====

Money Beleg::Offen() const {
    if (!brutto.Valid()) return Money::Invalid(waehrung);
    if (status == BelegStatus::Entwurf || status == BelegStatus::Storniert)
        return Money::Zero(waehrung);
    if (!bezahlt.Valid()) return brutto;
    return brutto - bezahlt;
}

std::vector<std::string> SteuerschluesselDesBelegs(const Beleg& beleg) {
    std::vector<std::string> keys;
    for (const BelegPosition& pos : beleg.positionen) {
        bool seen = false;
        for (const std::string& k : keys) {
            if (k == pos.steuerschluessel) { seen = true; break; }
        }
        if (!seen) keys.push_back(pos.steuerschluessel);
    }
    return keys;
}

bool Beleg::Summieren(const std::function<bool(const std::string&, int&)>& satzFuer) {
    if (satzFuer) {
        for (BelegPosition& pos : positionen) {
            int satz = 0;
            if (!satzFuer(pos.steuerschluessel, satz)) return false;
            pos.satzPromille = satz;
        }
    }
    return Summieren();
}

bool Beleg::Summieren() {
    if (positionen.empty()) {
        netto = steuer = brutto = Money::Zero(waehrung);
        return true;
    }

    // 1. Each position's own net. One rounding per line, and it is the figure
    //    the printed invoice shows, so it must be decided before anything is
    //    summed.
    for (BelegPosition& pos : positionen) {
        pos.netto = pos.NettoBetrag();
        if (!pos.netto.Valid()) return false;
    }

    // 2. Tax per rate, from the summed net of that rate - not per position.
    //    Grouping by the key rather than by the rate keeps two keys that happen
    //    to share 19 % (domestic revenue and a reverse-charge key, say) apart,
    //    because they post to different accounts and belong in different UStVA
    //    boxes.
    const std::vector<std::string> keys = SteuerschluesselDesBelegs(*this);

    Money summeNetto  = Money::Zero(waehrung);
    Money summeSteuer = Money::Zero(waehrung);

    for (const std::string& key : keys) {
        // The positions under this key, their summed net, and the rate. The
        // rate is taken from the first position of the group; a mismatch
        // within one key is a data error, and it is caught rather than
        // averaged.
        std::vector<size_t> indices;
        Money gruppeNetto = Money::Zero(waehrung);
        int   satz = -1;

        for (size_t i = 0; i < positionen.size(); ++i) {
            const BelegPosition& pos = positionen[i];
            if (pos.steuerschluessel != key) continue;
            if (satz < 0) satz = pos.satzPromille;
            else if (satz != pos.satzPromille) return false;
            indices.push_back(i);
            gruppeNetto = gruppeNetto + pos.netto;
            if (!gruppeNetto.Valid()) return false;
        }
        if (satz < 0) return false;

        const Money gruppeSteuer = gruppeNetto.TaxOnNet(satz);
        if (!gruppeSteuer.Valid()) return false;

        // 3. Hand that one tax figure back to the positions in proportion to
        //    their net, by largest remainder. The shares always add up to
        //    gruppeSteuer, so the invoice's line tax column sums to its tax
        //    total - which is the property a recipient's system checks.
        //
        //    Weights must be non-negative: a credit note has negative lines, so
        //    the split runs on magnitudes and the sign is restored afterwards.
        std::vector<int64_t> weights;
        weights.reserve(indices.size());
        for (size_t i : indices) {
            const int64_t minor = positionen[i].netto.Minor();
            weights.push_back(minor < 0 ? -minor : minor);
        }

        const std::vector<Money> anteile =
            gruppeSteuer.IsNegative()
                ? (Money::Zero(waehrung) - gruppeSteuer).SplitProportionally(weights)
                : gruppeSteuer.SplitProportionally(weights);
        if (anteile.size() != indices.size()) return false;

        for (size_t n = 0; n < indices.size(); ++n) {
            BelegPosition& pos = positionen[indices[n]];
            Money anteil = anteile[n];
            if (!anteil.Valid()) return false;
            // Restore the sign the group had, position by position: a negative
            // group means every share is negative.
            if (gruppeSteuer.IsNegative()) anteil = Money::Zero(waehrung) - anteil;
            pos.steuer = anteil;
            pos.brutto = pos.netto + pos.steuer;
            if (!pos.brutto.Valid()) return false;
        }

        summeNetto  = summeNetto + gruppeNetto;
        summeSteuer = summeSteuer + gruppeSteuer;
        if (!summeNetto.Valid() || !summeSteuer.Valid()) return false;
    }

    netto  = summeNetto;
    steuer = summeSteuer;
    brutto = summeNetto + summeSteuer;
    return brutto.Valid();
}

} // namespace UltraFIBU
