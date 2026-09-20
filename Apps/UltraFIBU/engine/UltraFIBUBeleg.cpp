// Apps/UltraFIBU/engine/UltraFIBUBeleg.cpp
// Enumeration text and the costing of a document. See the header for the two
// rounding rules this file exists to enforce.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUBeleg.h"

#include <cstdio>

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

bool GeldAbgangBeimAusgleich(BelegArt art) {
    switch (art) {
        case BelegArt::Ausgangsrechnung:   return false;  // the customer pays us
        case BelegArt::Eingangsrechnung:   return true;   // we pay the supplier
        case BelegArt::Ausgangsgutschrift: return true;   // we refund the customer
        case BelegArt::Eingangsgutschrift: return false;  // the supplier refunds us
        case BelegArt::Kassenbeleg:
        case BelegArt::Sonstiges:          return false;
    }
    return false;
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

// ===== IS THE TAX TREATMENT CONSISTENT WITH ITSELF? =====

namespace {

const Steuerschluessel* FindeKey(const std::vector<Steuerschluessel>& alle,
                                 const std::string& name) {
    for (const Steuerschluessel& key : alle)
        if (key.schluessel == name) return &key;
    return nullptr;
}

std::string SatzText(int promille) {
    char puffer[24];
    if (promille % 10 == 0) std::snprintf(puffer, sizeof(puffer), "%d %%", promille / 10);
    else std::snprintf(puffer, sizeof(puffer), "%d,%d %%", promille / 10, promille % 10);
    return puffer;
}

} // namespace

std::vector<SteuerBefund> PruefeSteuerlicheStimmigkeit(
    const Beleg& beleg, const Partner& partner,
    const std::vector<Steuerschluessel>& schluessel) {
    std::vector<SteuerBefund> befunde;
    const bool ausgang = IstAusgangsbeleg(beleg.art);

    for (const BelegPosition& pos : beleg.positionen) {
        SteuerBefund vorlage;
        vorlage.position   = pos.bezeichnung;
        vorlage.schluessel = pos.steuerschluessel;

        if (pos.steuerschluessel.empty()) {
            vorlage.text = "Die Position hat keinen Steuerschlüssel. Ohne ihn kann "
                           "die Rechnung weder einen Steuersatz noch einen Hinweis "
                           "auf die Steuerbefreiung ausweisen (§ 14 Abs. 4 Nr. 8 UStG).";
            vorlage.blockierend = true;
            befunde.push_back(vorlage);
            continue;
        }

        const Steuerschluessel* key = FindeKey(schluessel, pos.steuerschluessel);
        if (key == nullptr) {
            vorlage.text = "Den Steuerschlüssel \"" + pos.steuerschluessel +
                           "\" gibt es nicht. Ein Beleg, dessen Steuersatz niemand "
                           "beschreiben kann, darf nicht gebucht werden.";
            vorlage.blockierend = true;
            befunde.push_back(vorlage);
            continue;
        }

        // A key that was not in force on the document's date describes some
        // other year's law. Rates change; that is why they carry a validity.
        if (beleg.datum.Valid() && !key->GueltigAm(beleg.datum)) {
            SteuerBefund b = vorlage;
            b.text = "Der Steuerschlüssel \"" + key->schluessel + "\" gilt am " +
                     FormatDateGerman(beleg.datum) + " nicht. Ein Satz aus einem "
                     "anderen Zeitraum ergibt einen falschen Steuerbetrag.";
            b.blockierend = true;
            befunde.push_back(b);
        }

        // **The contradiction this whole function exists for.**
        if (ausgang && IstNullsatzImAusgang(key->art) && key->satzPromille != 0) {
            SteuerBefund b = vorlage;
            b.text = "\"" + key->bezeichnung + "\" weist " + SatzText(key->satzPromille) +
                     " Umsatzsteuer aus und sagt gleichzeitig, dass für diesen Umsatz "
                     "keine deutsche Umsatzsteuer berechnet wird. Eine Rechnung, die "
                     "beides tut, ist in jedem Fall falsch - entweder im Betrag oder "
                     "im Hinweis.";
            b.blockierend = true;
            befunde.push_back(b);
        }

        // An input-tax key on an outgoing document, or the reverse. The §13b
        // mix-up was exactly this shape.
        if (ausgang && key->vorsteuer) {
            SteuerBefund b = vorlage;
            b.text = "\"" + key->bezeichnung + "\" ist ein Vorsteuerschlüssel und "
                     "gehört auf einen Eingangsbeleg. Auf einer Ausgangsrechnung "
                     "bucht er die Steuer auf die falsche Seite.";
            b.blockierend = true;
            befunde.push_back(b);
        }

        // Zero-rating a cross-border B2B supply rests on the customer's VAT
        // number. Without it the supply is taxable here, and the invoice that
        // omitted it is the evidence.
        if (ausgang && BrauchtUstIdNrDesEmpfaengers(key->art) && partner.ustIdNr.empty()) {
            SteuerBefund b = vorlage;
            b.text = "\"" + key->bezeichnung + "\" setzt die USt-IdNr. des "
                     "Leistungsempfängers voraus - sie ist die Bedingung für die "
                     "Steuerfreiheit, nicht eine Formalie, und muss nach § 14a UStG "
                     "auf der Rechnung stehen. Bei " + partner.name + " ist keine "
                     "hinterlegt.";
            b.blockierend = true;
            befunde.push_back(b);
        }

        // The destination country is what an OSS line is for.
        if (key->art == SteuerArt::Oss && key->land.empty()) {
            SteuerBefund b = vorlage;
            b.text = "\"" + key->bezeichnung + "\" nennt kein Bestimmungsland. Eine "
                     "OSS-Meldung ohne Land kann nicht abgegeben werden.";
            b.blockierend = true;
            befunde.push_back(b);
        }

        // Charging German VAT to an EU business that gave a VAT number is
        // usually the wrong call - but it is legal (the customer may not have
        // given the number in time), so this warns rather than blocks.
        if (ausgang && key->art == SteuerArt::Inland && key->satzPromille != 0 &&
            partner.steuerkategorie == Steuerkategorie::EuUnternehmer &&
            !partner.ustIdNr.empty()) {
            SteuerBefund b = vorlage;
            b.text = partner.name + " ist als EU-Unternehmer mit USt-IdNr. " +
                     partner.ustIdNr + " erfasst, die Position berechnet aber "
                     "deutsche Umsatzsteuer. Für eine Lieferung wäre \"IGL\", für "
                     "eine sonstige Leistung \"EURC\" der Schlüssel.";
            b.blockierend = false;
            befunde.push_back(b);
        }

        // And the mirror image: zero-rating a domestic customer.
        if (ausgang && BrauchtUstIdNrDesEmpfaengers(key->art) &&
            partner.steuerkategorie == Steuerkategorie::Inland) {
            SteuerBefund b = vorlage;
            b.text = partner.name + " ist als Inlandskunde erfasst, der "
                     "Steuerschlüssel stellt die Position aber als "
                     "grenzüberschreitenden Umsatz steuerfrei. Eines von beidem "
                     "stimmt nicht.";
            b.blockierend = true;
            befunde.push_back(b);
        }
    }

    return befunde;
}

bool HatBlockierendenBefund(const std::vector<SteuerBefund>& befunde) {
    for (const SteuerBefund& b : befunde)
        if (b.blockierend) return true;
    return false;
}

// ===== WHICH TAX KEYS BELONG ON THIS DOCUMENT =====

namespace {

// What a key is for, reduced to the question the dropdown has to answer.
// Returns the sentence that goes under the dropdown, and sets `passend` when
// this key is one of the ones that fit.
// `vorgabe` is set only where there is an unambiguous normal answer. Where two
// suggestions are both correct and the choice turns on something the program
// does not know - goods or a service, above or below the § 3c threshold - it
// stays unset and the user has to choose. Picking one of two legally different
// treatments by list order is how an invoice ends up reverse-charged when it
// should have been an intra-community supply, and nothing would ever show it.
std::string Beurteile(const Steuerschluessel& key, const Mandant& mandant,
                      const Partner& partner, bool ausgang, bool& passend,
                      bool& vorgabe) {
    passend = false;
    vorgabe = false;

    // A § 19 business charges no VAT on anything it sells. That outranks every
    // other rule on an outgoing document, and getting it wrong means invoicing
    // tax the seller is not allowed to collect and then owes anyway.
    if (ausgang && mandant.kleinunternehmer) {
        if (key.art == SteuerArt::Kleinunternehmer) {
            passend = true;
            vorgabe = true;
            return "Als Kleinunternehmer nach § 19 UStG wird keine Umsatzsteuer "
                   "berechnet.";
        }
        if (key.satzPromille != 0)
            return "Als Kleinunternehmer darf keine Umsatzsteuer ausgewiesen werden - "
                   "ausgewiesene Steuer wird nach § 14c UStG trotzdem geschuldet.";
        return std::string();
    }

    // Input-tax keys belong on purchases and nowhere else.
    if (ausgang && key.vorsteuer)
        return "Vorsteuerschlüssel - gehört auf einen Eingangsbeleg.";

    // The mirror image, which is less obvious. On a purchase the only keys
    // that make sense are the ones that deduct input tax and the two that make
    // us the one who owes the German tax - the innergemeinschaftlicher Erwerb
    // and § 13b. An export key on an incoming invoice is the same category of
    // mistake as § 13b on an outgoing one, and it was in this list until a
    // Swiss supplier's invoice offered "Ausfuhrlieferung" as a suggestion.
    if (!ausgang && !key.vorsteuer &&
        key.art != SteuerArt::IgErwerb &&
        !(key.art == SteuerArt::ReverseCharge13b && key.satzPromille != 0))
        return "Schlüssel für Ausgangsbelege - auf einem Eingangsbeleg bucht er "
               "die Steuer auf die falsche Seite.";

    switch (partner.steuerkategorie) {
        case Steuerkategorie::Inland:
            if (key.art == SteuerArt::Inland) {
                passend = true;
                // The full rate is the rule and the reduced one the exception,
                // so 19 % is the default and 7 % is a decision.
                vorgabe = key.satzPromille == 190;
                if (key.satzPromille == 0)
                    return "Steuerfrei im Inland - nur mit einem Befreiungsgrund "
                           "nach § 4 UStG, der auf der Rechnung zu nennen ist.";
                return ausgang ? "Inlandsumsatz mit deutscher Umsatzsteuer."
                               : "Inlandsbezug mit abziehbarer Vorsteuer.";
            }
            if (ausgang && key.art == SteuerArt::ReverseCharge13b &&
                key.satzPromille == 0) {
                passend = true;
                return "Nur für die Fälle des § 13b UStG (z. B. Bauleistungen an "
                       "einen Unternehmer) - dann schuldet der Empfänger die Steuer.";
            }
            if (BrauchtUstIdNrDesEmpfaengers(key.art))
                return "Grenzüberschreitend steuerfrei - passt nicht zu einem "
                       "Inlandskunden.";
            return std::string();

        case Steuerkategorie::EuUnternehmer:
            // **The rule the customer asked about.** No German VAT, because the
            // customer accounts for it in their own country - but only against
            // their VAT number, which is the condition and not a formality.
            if (partner.ustIdNr.empty()) {
                if (key.art == SteuerArt::Inland && key.satzPromille != 0) {
                    passend = true;
                    vorgabe = key.satzPromille == 190;
                    return "Ohne USt-IdNr. des Kunden ist der Umsatz nicht steuerfrei: "
                           "bis sie vorliegt, wird deutsche Umsatzsteuer berechnet.";
                }
                if (BrauchtUstIdNrDesEmpfaengers(key.art))
                    return "Setzt die USt-IdNr. des Kunden voraus - sie fehlt in den "
                           "Stammdaten.";
                return std::string();
            }
            if (ausgang && key.art == SteuerArt::IgLieferung) {
                passend = true;
                return "WARE an ein EU-Unternehmen: steuerfrei nach § 4 Nr. 1b UStG, "
                       "der Kunde versteuert den Erwerb. (Für eine Dienstleistung "
                       "stattdessen den Reverse-Charge-Schlüssel wählen.)";
            }
            if (ausgang && key.art == SteuerArt::EuSonstigeLeistung) {
                passend = true;
                return "DIENSTLEISTUNG an ein EU-Unternehmen: Leistungsort ist das "
                       "Land des Kunden (§ 3a Abs. 2 UStG), keine deutsche "
                       "Umsatzsteuer, Hinweis auf die Steuerschuldnerschaft des "
                       "Leistungsempfängers auf der Rechnung. (Für eine Warenlieferung "
                       "stattdessen den innergemeinschaftlichen Schlüssel wählen.)";
            }
            if (!ausgang && key.art == SteuerArt::IgErwerb) {
                passend = true;
                return "Innergemeinschaftlicher Erwerb: die Steuer wird hier "
                       "angemeldet und zugleich als Vorsteuer abgezogen.";
            }
            if (!ausgang && key.art == SteuerArt::ReverseCharge13b &&
                key.satzPromille != 0) {
                passend = true;
                return "Sonstige Leistung aus dem EU-Ausland: wir schulden die "
                       "deutsche Steuer selbst (§ 13b UStG) und ziehen sie zugleich "
                       "als Vorsteuer ab.";
            }
            if (ausgang && key.art == SteuerArt::Inland && key.satzPromille != 0)
                return "Deutsche Umsatzsteuer an ein EU-Unternehmen mit USt-IdNr. - "
                       "möglich, aber meist ein Versehen.";
            return std::string();

        case Steuerkategorie::EuPrivat:
            // A private customer abroad is OSS territory, and the rate is the
            // destination country's, not ours.
            if (ausgang && key.art == SteuerArt::Oss &&
                (key.land.empty() || key.land == partner.land)) {
                passend = !key.land.empty();
                vorgabe = passend;
                return key.land.empty()
                           ? "One-Stop-Shop, aber ohne Bestimmungsland - für " +
                                 partner.land + " wird ein eigener Schlüssel gebraucht."
                           : "Privatkunde in " + key.land + ": Umsatzsteuer dieses "
                             "Landes, erklärt über den One-Stop-Shop.";
            }
            if (ausgang && key.art == SteuerArt::Inland && key.satzPromille != 0) {
                passend = true;
                // Deliberately phrased as the condition it is. Whether the
                // threshold has been crossed depends on the whole year's EU
                // turnover, which this function does not have - and stating it
                // as a fact would be the program vouching for something it did
                // not check. "ultrafibu lieferschwelle" is what checks it.
                return "Nur solange die Schwelle des § 3c UStG (10.000 EUR EU-weit) "
                       "nicht überschritten ist - darüber gilt der Satz des "
                       "Bestimmungslandes über den One-Stop-Shop.";
            }
            if (BrauchtUstIdNrDesEmpfaengers(key.art))
                return "Steuerfrei nur an Unternehmer - ein Privatkunde erfüllt die "
                       "Voraussetzung nicht.";
            return std::string();

        case Steuerkategorie::Drittland:
            if (ausgang && key.art == SteuerArt::Drittland) {
                passend = true;
                vorgabe = true;
                return "Ausfuhr in ein Drittland: steuerfrei nach § 4 Nr. 1a UStG, "
                       "der Ausfuhrnachweis gehört zum Beleg.";
            }
            if (!ausgang && key.art == SteuerArt::Drittland && key.vorsteuer) {
                passend = true;
                vorgabe = true;
                return "Einfuhr aus einem Drittland - die Einfuhrumsatzsteuer wird "
                       "getrennt vom Zoll erhoben.";
            }
            if (ausgang && key.art == SteuerArt::NichtSteuerbar) {
                passend = true;
                return "Leistungsort außerhalb Deutschlands: nicht steuerbar.";
            }
            if (BrauchtUstIdNrDesEmpfaengers(key.art))
                return "Innergemeinschaftlich - gilt nur innerhalb der EU.";
            return std::string();
    }
    return std::string();
}

} // namespace

std::vector<SteuerschluesselVorschlag> SteuerschluesselFuerPartner(
    const Mandant& mandant, const Partner& partner, BelegArt art, const Date& datum,
    const std::vector<Steuerschluessel>& alle) {
    const bool ausgang = IstAusgangsbeleg(art);

    // The document that proves whether a key contradicts this partner is the
    // same one the posting check reads, so the two can never disagree: build a
    // one-line document and ask it.
    auto widerspricht = [&](const Steuerschluessel& key) {
        Beleg probe;
        probe.art          = art;
        probe.datum        = datum;
        probe.partnerKonto = partner.konto;
        BelegPosition pos;
        pos.bezeichnung      = "Probe";
        pos.steuerschluessel = key.schluessel;
        probe.positionen.push_back(pos);
        return HatBlockierendenBefund(
            PruefeSteuerlicheStimmigkeit(probe, partner, { key }));
    };

    std::vector<SteuerschluesselVorschlag> passende, uebrige;
    for (const Steuerschluessel& key : alle) {
        // A key that was not in force on the day is not offered at all.
        if (datum.Valid() && !key.GueltigAm(datum)) continue;

        SteuerschluesselVorschlag v;
        v.schluessel  = key;
        bool vorgabe = false;
        v.begruendung = Beurteile(key, mandant, partner, ausgang, v.passend, vorgabe);
        v.vorgabe     = vorgabe;
        v.widerspruch = widerspricht(key);
        // A key that contradicts the partner is never a suggestion, whatever
        // the reasoning above concluded - the posting check has the last word,
        // so that the dropdown cannot recommend something Buchen will refuse.
        if (v.widerspruch) { v.passend = false; v.vorgabe = false; }
        (v.passend ? passende : uebrige).push_back(std::move(v));
    }

    // At most one default, and no fallback to "the first one". Where the rules
    // above named none, none is offered: an empty dropdown selection makes the
    // user choose, which is the correct outcome when the choice depends on
    // something only they know.
    bool schonEine = false;
    for (SteuerschluesselVorschlag& v : passende) {
        if (!v.vorgabe) continue;
        if (schonEine) v.vorgabe = false;
        schonEine = true;
    }

    std::vector<SteuerschluesselVorschlag> ergebnis;
    ergebnis.reserve(passende.size() + uebrige.size());
    for (SteuerschluesselVorschlag& v : passende) ergebnis.push_back(std::move(v));
    for (SteuerschluesselVorschlag& v : uebrige)  ergebnis.push_back(std::move(v));
    return ergebnis;
}

} // namespace UltraFIBU
