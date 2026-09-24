// Apps/UltraFIBU/ui/UltraFIBUBelegDialog.cpp
// The document form. See the header for why the tax dropdown is the centre of
// it rather than one field among many.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUBelegDialog.h"

#include <cstdio>

namespace UltraFIBU {

using namespace UltraCanvas;

namespace {

constexpr float kZeit       = 26.0f;   // one form row
constexpr float kZeilenH    = 46.0f;   // one position row, including its hint
constexpr float kRand       = 12.0f;

// The position grid's column geometry, in one place so the header labels and
// the fields cannot drift apart - which they did, twice, while this was two
// lists of numbers.
struct Spalte { float x; float breite; const char* kopf; };
const Spalte kSpalten[] = {
    { 0.0f,   28.0f,  "Pos" },
    { 30.0f,  288.0f, "Bezeichnung" },
    { 322.0f,  56.0f, "Menge" },
    { 382.0f,  48.0f, "Einheit" },
    { 434.0f,  88.0f, "Preis" },
    { 526.0f,  56.0f, "Rabatt %" },
    { 586.0f,  66.0f, "Konto" },
    { 656.0f, 300.0f, "Steuer" },
    { 960.0f, 118.0f, "Betrag" },
};
// Named indices, because the columns moved twice while they were bare numbers
// and a field ended up under the wrong header each time.
enum SpaltenIndex { kPos, kBezeichnung, kMenge, kEinheit, kPreis, kRabatt, kKonto,
                    kSteuer, kBetrag };

std::string Zahl(int64_t wert) {
    char puffer[32];
    std::snprintf(puffer, sizeof(puffer), "%lld", static_cast<long long>(wert));
    return puffer;
}

std::string SatzText(int promille) {
    char puffer[24];
    if (promille % 10 == 0) std::snprintf(puffer, sizeof(puffer), "%d %%", promille / 10);
    else std::snprintf(puffer, sizeof(puffer), "%d,%d %%", promille / 10, promille % 10);
    return puffer;
}

} // namespace

BelegDialog::BelegDialog(Store& store, const Mandant& mandant, const Akteur& akteur)
    : store_(store), mandant_(mandant), akteur_(akteur) {}

// ===== BUILDING =====

std::shared_ptr<UltraCanvasContainer> BelegDialog::Bauen(const std::string& id,
                                                         float breite, float hoehe,
                                                         BelegArt art) {
    art_ = art;
    wurzel_ = CreateContainer(id, 0, 0, breite, hoehe);

    float y = kRand;

    titel_ = CreateLabel(id + "Titel", kRand, y, breite - 2 * kRand, kZeit,
                         BelegArtLabel(art_) + " - Entwurf");
    wurzel_->AddChild(titel_);
    y += kZeit + 6.0f;

    // ---- the partner, and what follows from it ----
    //
    // First field on the form on purpose: it decides which tax keys the rest of
    // the form may offer, so choosing it later would mean re-answering every
    // line's tax question.
    partnerLabel_ = CreateLabel(id + "KundeL", kRand, y, 110, kZeit, "Kunde");
    wurzel_->AddChild(partnerLabel_);
    partnerWahl_ = CreateDropdown(id + "Kunde", kRand + 114, y, 330, kZeit);
    partnerWahl_->onSelectionChanged = [this](int index, const DropdownItem&) {
        PartnerGewaehlt(index);
    };
    wurzel_->AddChild(partnerWahl_);

    // Country, VAT number and what they imply, in one line. This is the
    // equivalent of the reference program's "Land" field plus its hint box: the
    // partner's country is not editable here because it is master data, and an
    // invoice that quietly used a different country from the customer record
    // would be unexplainable afterwards.
    partnerInfo_ = CreateLabel(id + "KundeInfo", kRand + 452, y, breite - kRand - 464,
                               kZeit, "");
    wurzel_->AddChild(partnerInfo_);
    y += kZeit + 4.0f;

    // ---- dates and references ----
    // **Brutto or Netto.** A supplier's receipt states gross; typing that into
    // a net field overstates the expense by the tax, on every receipt, and
    // nothing downstream notices. So it is a switch and not an assumption, and
    // it defaults to gross on an incoming document because that is what those
    // documents state.
    wurzel_->AddChild(CreateLabel(id + "PreisartL", kRand, y, 110, kZeit, "Preise sind"));
    preisart_ = CreateDropdown(id + "Preisart", kRand + 114, y, 130, kZeit);
    preisart_->AddItem("Netto", "netto");
    preisart_->AddItem("Brutto", "brutto");
    preisart_->SetSelectedIndex(0, false);
    preisart_->onSelectionChanged = [this](int, const DropdownItem&) { Neuberechnen(); };
    wurzel_->AddChild(preisart_);

    // The tax as the document states it. Always built, shown only on a
    // document we received - on one we issue we are the ones deciding, so
    // there is nothing to copy off it.
    steuerLautLabel_ = CreateLabel(id + "StLautL", kRand + 260, y, 190, kZeit,
                                   "Steuer laut Beleg");
    wurzel_->AddChild(steuerLautLabel_);
    steuerLaut_ = CreateTextInput(id + "StLaut", static_cast<int>(kRand + 454),
                                  static_cast<int>(y), 110, static_cast<int>(kZeit));
    steuerLaut_->SetPlaceholder("wie berechnet");
    steuerLaut_->onTextChanged = [this](const std::string&) { Neuberechnen(); };
    wurzel_->AddChild(steuerLaut_);
    steuerLautHinweis_ = CreateLabel(
        id + "StLautH", kRand + 574, y, breite - kRand - 586, kZeit,
        "Leer lassen rechnet selbst. Weicht der Beleg ab, gilt der Beleg.");
    wurzel_->AddChild(steuerLautHinweis_);
    y += kZeit + 4.0f;

    wurzel_->AddChild(CreateLabel(id + "DatumL", kRand, y, 110, kZeit, "Belegdatum"));
    datum_ = CreateTextInput(id + "Datum", static_cast<int>(kRand + 114),
                             static_cast<int>(y), 130, static_cast<int>(kZeit));
    datum_->SetPlaceholder("TT.MM.JJJJ");
    wurzel_->AddChild(datum_);

    externLabel_ = CreateLabel(id + "ExtL", kRand + 260, y, 190, kZeit, "Ihre Referenz");
    wurzel_->AddChild(externLabel_);
    externeNummer_ = CreateTextInput(id + "Ext", static_cast<int>(kRand + 454),
                                     static_cast<int>(y), 180, static_cast<int>(kZeit));
    wurzel_->AddChild(externeNummer_);
    y += kZeit + 4.0f;

    // § 14 Abs. 4 Nr. 6: a selection rather than a bare date field, because
    // "geliefert am" and "geleistet im Zeitraum" are different statements and
    // the recipient books from whichever one is printed.
    wurzel_->AddChild(CreateLabel(id + "LArtL", kRand, y, 110, kZeit,
                                  "Lieferung/Leistung"));
    leistungsart_ = CreateDropdown(id + "LArt", kRand + 114, y, 200, kZeit);
    for (const Leistungszeitpunkt art : { Leistungszeitpunkt::Lieferdatum,
                                          Leistungszeitpunkt::Leistungsdatum,
                                          Leistungszeitpunkt::Lieferzeitraum,
                                          Leistungszeitpunkt::Leistungszeitraum,
                                          Leistungszeitpunkt::Keiner }) {
        leistungsart_->AddItem(LeistungszeitpunktLabel(art),
                               LeistungszeitpunktToText(art));
    }
    leistungsart_->SetSelectedIndex(1, false);   // Leistungsdatum
    leistungsart_->onSelectionChanged = [this](int, const DropdownItem&) { Neuberechnen(); };
    wurzel_->AddChild(leistungsart_);

    leistungsdatum_ = CreateTextInput(id + "Leist", static_cast<int>(kRand + 322),
                                      static_cast<int>(y), 130, static_cast<int>(kZeit));
    leistungsdatum_->SetPlaceholder("TT.MM.JJJJ");
    wurzel_->AddChild(leistungsdatum_);

    wurzel_->AddChild(CreateLabel(id + "BisL", kRand + 458, y, 24, kZeit, "bis"));
    leistungBis_ = CreateTextInput(id + "LeistBis", static_cast<int>(kRand + 484),
                                   static_cast<int>(y), 130, static_cast<int>(kZeit));
    leistungBis_->SetPlaceholder("nur bei Zeitraum");
    wurzel_->AddChild(leistungBis_);
    y += kZeit + 4.0f;

    wurzel_->AddChild(CreateLabel(id + "BetreffL", kRand, y, 110, kZeit, "Betreff"));
    betreff_ = CreateTextInput(id + "Betreff", static_cast<int>(kRand + 114),
                               static_cast<int>(y), 750, static_cast<int>(kZeit));
    wurzel_->AddChild(betreff_);
    y += kZeit + 10.0f;

    // ---- the position grid's header ----
    for (const Spalte& s : kSpalten) {
        wurzel_->AddChild(CreateLabel(id + "H" + s.kopf, kRand + s.x, y, s.breite,
                                      kZeit, s.kopf));
    }
    y += kZeit;

    zeilenBereich_ = CreateContainer(id + "Zeilen", 0, y, breite, hoehe - y - 120.0f);
    wurzel_->AddChild(zeilenBereich_);
    naechsteZeileY_ = 0.0f;

    // ---- totals, findings, save ----
    const float unten = hoehe - 108.0f;
    neueZeile_ = CreateButton(id + "Plus", kRand, unten, 170, kZeit, "+ Neue Position");
    neueZeile_->SetOnClick([this]() { ZeileAnlegen(); Neuberechnen(); });
    wurzel_->AddChild(neueZeile_);

    summen_ = CreateLabel(id + "Summen", kRand + 560, unten, breite - kRand - 572,
                          kZeit, "");
    wurzel_->AddChild(summen_);

    // Two lines, because a finding is a sentence and not a code.
    befunde_ = CreateLabel(id + "Befunde", kRand, unten + kZeit + 4.0f,
                           breite - 2 * kRand, 44.0f, "");
    wurzel_->AddChild(befunde_);

    speichern_ = CreateButton(id + "Speichern", breite - kRand - 200, unten + kZeit + 26.0f,
                              200, 30.0f, "Beleg speichern");
    speichern_->SetOnClick([this]() { Speichern(); });
    wurzel_->AddChild(speichern_);

    RichtungAnwenden();
    PartnerListeFuellen();
    ZeileAnlegen();
    Neuberechnen();
    return wurzel_;
}

// ===== PARTNER =====

void BelegDialog::PartnerListeFuellen() {
    if (!partnerWahl_) return;
    partnerWahl_->ClearItems();
    // Customers for an outgoing document, suppliers for an incoming one. A
    // supplier offered as the customer of an invoice is not a small mistake:
    // it books to the wrong side of the ledger.
    const PartnerTyp typ = IstAusgangsbeleg(art_) ? PartnerTyp::Kunde : PartnerTyp::Lieferant;
    partnerListe_ = store_.PartnerListe(mandant_.id, typ);
    partnerWahl_->AddItem("- bitte wählen -", "");
    for (const Partner& p : partnerListe_)
        partnerWahl_->AddItem(p.konto + "  " + p.name, p.konto);
    partnerWahl_->SetSelectedIndex(0, false);
}

void BelegDialog::PartnerGewaehlt(int index) {
    partner_ = Partner();
    if (index > 0 && static_cast<size_t>(index - 1) < partnerListe_.size())
        partner_ = partnerListe_[index - 1];

    if (partnerInfo_) {
        if (partner_.id == 0) {
            partnerInfo_->SetText("");
        } else {
            std::string text = "Land " + partner_.land + "  ·  " +
                               SteuerkategorieToText(partner_.steuerkategorie);
            // Said plainly, because the absence of the number is what decides
            // whether this document can be zero-rated at all.
            text += partner_.ustIdNr.empty()
                        ? "  ·  keine USt-IdNr. hinterlegt"
                        : "  ·  USt-IdNr. " + partner_.ustIdNr;
            partnerInfo_->SetText(text);
        }
    }

    // Every line's tax choice depends on the partner, so every line's list is
    // rebuilt. A stale list is how a key for the previous customer stays
    // selected after the customer changes.
    for (PositionsZeile& zeile : zeilen_) SteuerlisteFuellen(zeile);
    Neuberechnen();
}

// ===== POSITIONS =====

void BelegDialog::ZeileAnlegen() {
    if (!zeilenBereich_) return;
    const std::string id = "pos" + Zahl(static_cast<int64_t>(zeilen_.size()) + 1);
    const float y = naechsteZeileY_;
    PositionsZeile zeile;

    auto feld = [&](const Spalte& s, const std::string& platzhalter) {
        auto f = CreateTextInput(id + s.kopf, static_cast<int>(kRand + s.x),
                                 static_cast<int>(y), static_cast<int>(s.breite), 24);
        if (!platzhalter.empty()) f->SetPlaceholder(platzhalter);
        f->onTextChanged = [this](const std::string&) { Neuberechnen(); };
        zeilenBereich_->AddChild(f);
        return f;
    };

    zeile.nummer = CreateLabel(id + "Nr", kRand + kSpalten[kPos].x, y, kSpalten[kPos].breite,
                               24, Zahl(static_cast<int64_t>(zeilen_.size()) + 1) + ".");
    zeilenBereich_->AddChild(zeile.nummer);

    zeile.bezeichnung = feld(kSpalten[kBezeichnung], "Leistung oder Ware");
    zeile.menge       = feld(kSpalten[kMenge], "1");
    zeile.einheit     = feld(kSpalten[kEinheit], "Stk");
    zeile.preis       = feld(kSpalten[kPreis], "0,00");
    zeile.rabatt      = feld(kSpalten[kRabatt], "0");
    zeile.konto       = feld(kSpalten[kKonto], "8400");

    zeile.steuer = CreateDropdown(id + "Steuer", kRand + kSpalten[kSteuer].x, y,
                                  kSpalten[kSteuer].breite, 24);
    {
        // The stock 400 px cuts "Leistungsempfaenger schuldet die Steuer,
        // Ausgang (§ 13b UStG)" in half, and the half that survives is the
        // half both § 13b keys share - so the list would show two entries that
        // read alike and mean opposite things.
        DropdownStyle stil;
        stil.maxItemWidth   = 620.0f;
        stil.maxVisibleItems = 12;
        zeile.steuer->SetStyle(stil);
    }
    const size_t meinIndex = zeilen_.size();
    zeile.steuer->onSelectionChanged = [this, meinIndex](int, const DropdownItem&) {
        if (meinIndex < zeilen_.size()) SteuerHinweisAktualisieren(zeilen_[meinIndex]);
        Neuberechnen();
    };
    zeilenBereich_->AddChild(zeile.steuer);

    zeile.betrag = CreateLabel(id + "Betrag", kRand + kSpalten[kBetrag].x, y,
                               kSpalten[kBetrag].breite, 24, "0,00");
    zeilenBereich_->AddChild(zeile.betrag);

    // The sentence sits under the dropdown, across the width of the grid,
    // because it is a sentence and not a cell.
    zeile.hinweis = CreateLabel(id + "Hinweis", kRand + kSpalten[kBezeichnung].x, y + 24.0f,
                                kSpalten[kBetrag].x - kSpalten[kBezeichnung].x, 20.0f, "");
    zeilenBereich_->AddChild(zeile.hinweis);

    zeilen_.push_back(zeile);
    SteuerlisteFuellen(zeilen_.back());
    naechsteZeileY_ += kZeilenH;
}

void BelegDialog::SteuerlisteFuellen(PositionsZeile& zeile) {
    if (!zeile.steuer) return;

    // The date matters: keys carry a validity, and the list for a document
    // dated in March is not necessarily the list for one dated in July.
    Date datum;
    if (datum_ && !TryParseDateGerman(datum_->GetText(), datum)) datum = Date();

    // **No partner, no tax list.** A default-constructed Partner has the
    // domestic category, so asking anyway produces a confident "USt19 19 %" for
    // a customer nobody has chosen yet - and a line could be typed and saved
    // under it. An empty dropdown is the honest state: the question cannot be
    // answered before the partner is known.
    if (partner_.id == 0) {
        zeile.vorschlaege.clear();
        zeile.steuer->ClearItems();
        zeile.steuer->SetSelectedIndex(-1, false);
        SteuerHinweisAktualisieren(zeile);
        return;
    }

    zeile.vorschlaege = SteuerschluesselFuerPartner(mandant_, partner_, art_, datum,
                                                    schluessel_.empty()
                                                        ? store_.SteuerschluesselListe(mandant_.id)
                                                        : schluessel_);

    zeile.steuer->ClearItems();
    int vorgabeIndex = -1;
    int i = 0;
    for (const SteuerschluesselVorschlag& v : zeile.vorschlaege) {
        // Marked in the label, so the state is visible with the list closed:
        // a suggestion, a contradiction, or neither.
        std::string label = v.schluessel.schluessel + "  " +
                            SatzText(v.schluessel.satzPromille);
        if (v.widerspruch)   label = "! " + label;
        else if (v.passend)  label = "✓ " + label;
        else                 label = "  " + label;
        label += "  " + v.schluessel.bezeichnung;
        zeile.steuer->AddItem(label, v.schluessel.schluessel);
        if (v.vorgabe) vorgabeIndex = i;
        ++i;
    }

    // **No fallback to the first entry.** Where SteuerschluesselFuerPartner
    // named no default - goods or a service, which it cannot know - the field
    // stays empty and the user has to choose. Selecting something plausible on
    // their behalf is how the wrong one of two legally different treatments
    // ends up on an invoice with nothing to show for it.
    if (vorgabeIndex >= 0) zeile.steuer->SetSelectedIndex(vorgabeIndex, false);
    SteuerHinweisAktualisieren(zeile);
}

void BelegDialog::SteuerHinweisAktualisieren(PositionsZeile& zeile) {
    if (!zeile.hinweis) return;
    const int index = zeile.steuer ? zeile.steuer->GetSelectedIndex() : -1;
    if (index < 0 || static_cast<size_t>(index) >= zeile.vorschlaege.size()) {
        zeile.hinweis->SetText(
            partner_.id == 0
                ? "Zuerst den Partner wählen - er entscheidet, welche Steuerschlüssel "
                  "in Frage kommen."
                : "Kein Steuerschlüssel gewählt. Ohne ihn kann die Rechnung weder einen "
                  "Steuersatz noch einen Befreiungshinweis ausweisen (§ 14 UStG).");
        return;
    }
    const SteuerschluesselVorschlag& v = zeile.vorschlaege[index];
    std::string text = v.begruendung;
    if (v.widerspruch)
        text = "Widerspricht diesem Partner - das Buchen wird abgelehnt. " + text;
    zeile.hinweis->SetText(text);
}

// ===== COSTING =====

bool BelegDialog::BelegAusFormular(Beleg& out, std::string& fehler) const {
    out = Beleg();
    out.id        = belegId_;
    out.mandantId = mandant_.id;
    out.art       = art_;
    out.nummer    = belegNummer_;

    if (!datum_ || !TryParseDateGerman(datum_->GetText(), out.datum)) {
        fehler = "Das Belegdatum fehlt oder ist kein Datum (TT.MM.JJJJ).";
        return false;
    }
    if (leistungsart_) {
        const DropdownItem* gewaehlt = leistungsart_->GetSelectedItem();
        if (gewaehlt != nullptr)
            LeistungszeitpunktFromText(gewaehlt->value, out.leistungsart);
    }
    if (leistungsdatum_ && !leistungsdatum_->GetText().empty())
        TryParseDateGerman(leistungsdatum_->GetText(), out.leistungVon);
    if (leistungBis_ && !leistungBis_->GetText().empty())
        TryParseDateGerman(leistungBis_->GetText(), out.leistungBis);

    out.preiseSindBrutto = preisart_ && preisart_->GetSelectedIndex() == 1;

    // An empty field means "compute it". A figure means the document states
    // its own tax and that figure wins - which is the point, so a value that
    // cannot be read is an error rather than a silent fallback to computing.
    if (steuerLaut_ && !IstAusgangsbeleg(art_) && !steuerLaut_->GetText().empty()) {
        Money angegeben;
        if (!Money::TryParse(steuerLaut_->GetText(), angegeben)) {
            fehler = "\"" + steuerLaut_->GetText() + "\" ist kein Steuerbetrag.";
            return false;
        }
        out.steuerVorgegeben  = true;
        out.vorgegebeneSteuer = angegeben;
    }

    if (partner_.id == 0) {
        fehler = std::string(IstAusgangsbeleg(art_) ? "Kunde" : "Lieferant") +
                 " ist nicht gewählt.";
        return false;
    }
    out.partnerId    = partner_.id;
    out.partnerKonto = partner_.konto;
    out.partnerName  = partner_.name;
    out.externeNummer = externeNummer_ ? externeNummer_->GetText() : "";
    out.buchungstext  = betreff_ ? betreff_->GetText() : "";

    for (const PositionsZeile& zeile : zeilen_) {
        if (!zeile.bezeichnung || zeile.bezeichnung->GetText().empty()) continue;
        BelegPosition pos;
        pos.bezeichnung = zeile.bezeichnung->GetText();

        // Quantity through Money and scaled, exactly as the command line does
        // it: 0,25 hours has to stay exact and no double may enter.
        Money menge;
        const std::string mengeText = zeile.menge ? zeile.menge->GetText() : "";
        if (mengeText.empty()) pos.mengeTausendstel = 1000;
        else if (Money::TryParse(mengeText, menge) && menge.Minor() != 0)
            pos.mengeTausendstel = menge.Minor() * 10;
        else {
            fehler = "\"" + mengeText + "\" ist keine Menge (Position \"" +
                     pos.bezeichnung + "\").";
            return false;
        }

        Money preis;
        const std::string preisText = zeile.preis ? zeile.preis->GetText() : "";
        if (!Money::TryParse(preisText, preis)) {
            fehler = "\"" + preisText + "\" ist kein Betrag (Position \"" +
                     pos.bezeichnung + "\").";
            return false;
        }
        pos.einzelpreis = preis;

        // Per mille internally, entered as a percentage: 5 -> 50. Read through
        // Money for the same reason the quantity is - one decimal has to stay
        // exact and no double may enter.
        const std::string rabattText = zeile.rabatt ? zeile.rabatt->GetText() : "";
        if (!rabattText.empty()) {
            Money rabatt;
            if (!Money::TryParse(rabattText, rabatt) || rabatt.Minor() < 0 ||
                rabatt.Minor() > 10000) {
                fehler = "\"" + rabattText + "\" ist kein Rabatt zwischen 0 und 100 % "
                         "(Position \"" + pos.bezeichnung + "\").";
                return false;
            }
            pos.rabattPromille = static_cast<int>(rabatt.Minor() / 10);
        }

        if (zeile.einheit && !zeile.einheit->GetText().empty())
            pos.einheit = zeile.einheit->GetText();
        pos.konto = zeile.konto ? zeile.konto->GetText() : "";

        const int index = zeile.steuer ? zeile.steuer->GetSelectedIndex() : -1;
        if (index >= 0 && static_cast<size_t>(index) < zeile.vorschlaege.size())
            pos.steuerschluessel = zeile.vorschlaege[index].schluessel.schluessel;

        out.positionen.push_back(std::move(pos));
    }

    if (out.positionen.empty()) {
        fehler = "Der Beleg hat keine Position mit einer Bezeichnung.";
        return false;
    }
    return true;
}

void BelegDialog::Neuberechnen() {
    Beleg beleg;
    std::string fehler;
    if (!BelegAusFormular(beleg, fehler)) {
        if (summen_) summen_->SetText("Netto 0,00   Steuer 0,00   Gesamt 0,00");
        if (befunde_) befunde_->SetText(fehler);
        for (PositionsZeile& zeile : zeilen_)
            if (zeile.betrag) zeile.betrag->SetText("0,00");
        return;
    }

    const std::vector<Steuerschluessel> keys = store_.SteuerschluesselListe(mandant_.id);
    beleg.Summieren([&](const std::string& name, int& satz) {
        for (const Steuerschluessel& k : keys) {
            if (k.schluessel != name) continue;
            if (beleg.datum.Valid() && !k.GueltigAm(beleg.datum)) continue;
            satz = k.satzPromille;
            return true;
        }
        return false;
    });

    size_t i = 0;
    for (PositionsZeile& zeile : zeilen_) {
        if (!zeile.bezeichnung || zeile.bezeichnung->GetText().empty()) continue;
        if (i < beleg.positionen.size() && zeile.betrag) {
            const BelegPosition& pos = beleg.positionen[i];
            zeile.betrag->SetText((beleg.preiseSindBrutto ? pos.brutto : pos.netto)
                                      .ToString());
        }
        ++i;
    }
    if (summen_) {
        std::string text = "Netto " + beleg.netto.ToString() +
                           "   Steuer " + beleg.steuer.ToString() +
                           "   Gesamt " + beleg.brutto.ToString();
        // Said out loud, because the same three numbers mean different things
        // depending on which way the switch stands, and the one that matters
        // on a receipt is whether the total equals the amount that was paid.
        text += beleg.preiseSindBrutto ? "   (Preise brutto)" : "   (Preise netto)";
        if (beleg.steuerVorgegeben) text += "   Steuer lt. Beleg";
        summen_->SetText(text);
    }

    // The same check the store runs, shown while there is still time to change
    // the choice rather than as a refusal on save.
    if (befunde_) {
        const std::vector<SteuerBefund> befunde =
            PruefeSteuerlicheStimmigkeit(beleg, partner_, keys);
        std::string text;
        for (const SteuerBefund& b : befunde) {
            if (!text.empty()) text += "   ";
            text += (b.blockierend ? "STOPP: " : "Hinweis: ") + b.text;
        }
        befunde_->SetText(text);
    }
}

// ===== SAVING =====

void BelegDialog::Speichern() {
    Beleg beleg;
    std::string fehler;
    if (!BelegAusFormular(beleg, fehler)) {
        if (onMeldung) onMeldung(fehler);
        return;
    }

    // Not the guard - SaveBeleg checks independently - but the message is
    // better here, where the field that caused it is on screen.
    const std::vector<SteuerBefund> befunde = PruefeSteuerlicheStimmigkeit(
        beleg, partner_, store_.SteuerschluesselListe(mandant_.id));
    if (HatBlockierendenBefund(befunde)) {
        for (const SteuerBefund& b : befunde) {
            if (!b.blockierend) continue;
            if (onMeldung) onMeldung(b.text);
            return;
        }
    }

    const std::string kreis = IstAusgangsbeleg(art_) ? "rechnung" : "eingang";
    const StoreResult r = store_.SaveBeleg(beleg, kreis, akteur_);
    if (!r) {
        if (onMeldung) onMeldung(r.fehler);
        return;
    }
    belegId_     = beleg.id;
    belegNummer_ = beleg.nummer;
    if (titel_) titel_->SetText(BelegArtLabel(art_) + " " + beleg.nummer + " - Entwurf");
    if (onMeldung)
        onMeldung(BelegArtLabel(art_) + " " + beleg.nummer + " gespeichert (" +
                  beleg.brutto.ToString() + "). Mit \"Beleg buchen\" wird daraus eine "
                  "Buchung - danach ist sie nur noch stornierbar.");
    if (onGespeichert) onGespeichert(beleg);
}

void BelegDialog::RichtungAnwenden() {
    const bool ausgang = IstAusgangsbeleg(art_);
    if (partnerLabel_) partnerLabel_->SetText(ausgang ? "Kunde" : "Lieferant");
    if (externLabel_)
        externLabel_->SetText(ausgang ? "Ihre Referenz" : "Rechnungsnr. des Lieferanten");
    // A receipt states gross; an invoice we write is priced net. Defaulting the
    // switch to what the document in hand actually shows is the difference
    // between a correct expense and one overstated by the tax, every time.
    if (preisart_) preisart_->SetSelectedIndex(ausgang ? 0 : 1, false);
    if (steuerLautLabel_)   steuerLautLabel_->SetVisible(!ausgang);
    if (steuerLaut_)        steuerLaut_->SetVisible(!ausgang);
    if (steuerLautHinweis_) steuerLautHinweis_->SetVisible(!ausgang);
}

void BelegDialog::Neu(BelegArt art) {
    art_ = art;
    belegId_ = 0;
    belegNummer_.clear();
    FormularLeeren();
    RichtungAnwenden();
    PartnerListeFuellen();
    if (titel_) titel_->SetText(BelegArtLabel(art_) + " - Entwurf");
    Neuberechnen();
}

void BelegDialog::FormularLeeren() {
    if (datum_)          datum_->SetText("");
    if (leistungsdatum_) leistungsdatum_->SetText("");
    if (leistungBis_)    leistungBis_->SetText("");
    if (steuerLaut_)     steuerLaut_->SetText("");
    if (externeNummer_)  externeNummer_->SetText("");
    if (betreff_)        betreff_->SetText("");
    for (PositionsZeile& zeile : zeilen_) {
        if (zeile.bezeichnung) zeile.bezeichnung->SetText("");
        if (zeile.menge)       zeile.menge->SetText("");
        if (zeile.einheit)     zeile.einheit->SetText("");
        if (zeile.preis)       zeile.preis->SetText("");
        if (zeile.rabatt)      zeile.rabatt->SetText("");
        if (zeile.konto)       zeile.konto->SetText("");
    }
    partner_ = Partner();
}

bool BelegDialog::Laden(int64_t belegId) {
    Beleg beleg;
    if (!store_.BelegById(belegId, beleg)) return false;
    // A posted document is not editable - the store would refuse the write, and
    // a form that lets somebody type into it and then loses the change is worse
    // than one that says so.
    if (beleg.status != BelegStatus::Entwurf) {
        if (onMeldung)
            onMeldung("Der Beleg " + beleg.nummer + " ist bereits gebucht und kann "
                      "nicht mehr geändert werden. Eine Änderung ist eine Stornierung.");
        return false;
    }

    art_         = beleg.art;
    belegId_     = beleg.id;
    belegNummer_ = beleg.nummer;
    FormularLeeren();
    RichtungAnwenden();
    PartnerListeFuellen();

    if (datum_)          datum_->SetText(FormatDateGerman(beleg.datum));
    if (leistungsdatum_ && beleg.leistungVon.Valid())
        leistungsdatum_->SetText(FormatDateGerman(beleg.leistungVon));
    if (leistungBis_ && beleg.leistungBis.Valid())
        leistungBis_->SetText(FormatDateGerman(beleg.leistungBis));
    if (preisart_) preisart_->SetSelectedIndex(beleg.preiseSindBrutto ? 1 : 0, false);
    if (steuerLaut_ && beleg.steuerVorgegeben)
        steuerLaut_->SetText(beleg.vorgegebeneSteuer.ToString(MoneyStyle::Plain));
    if (leistungsart_) {
        const std::string wert = LeistungszeitpunktToText(beleg.leistungsart);
        for (int i = 0; i < 5; ++i) {
            const DropdownItem* item = nullptr;
            leistungsart_->SetSelectedIndex(i, false);
            item = leistungsart_->GetSelectedItem();
            if (item != nullptr && item->value == wert) break;
        }
    }
    if (externeNummer_)  externeNummer_->SetText(beleg.externeNummer);
    if (betreff_)        betreff_->SetText(beleg.buchungstext);
    if (titel_) titel_->SetText(BelegArtLabel(art_) + " " + beleg.nummer + " bearbeiten");

    for (size_t i = 0; i < partnerListe_.size(); ++i) {
        if (partnerListe_[i].konto != beleg.partnerKonto) continue;
        if (partnerWahl_) partnerWahl_->SetSelectedIndex(static_cast<int>(i) + 1, false);
        partner_ = partnerListe_[i];
        break;
    }
    if (partnerInfo_) PartnerGewaehlt(partnerWahl_ ? partnerWahl_->GetSelectedIndex() : 0);

    while (zeilen_.size() < beleg.positionen.size()) ZeileAnlegen();
    for (size_t i = 0; i < beleg.positionen.size() && i < zeilen_.size(); ++i) {
        const BelegPosition& pos = beleg.positionen[i];
        PositionsZeile& zeile = zeilen_[i];
        if (zeile.bezeichnung) zeile.bezeichnung->SetText(pos.bezeichnung);
        if (zeile.menge) {
            const Money menge = Money::FromMinor(pos.mengeTausendstel / 10, "EUR");
            zeile.menge->SetText(menge.ToString(MoneyStyle::Plain));
        }
        if (zeile.einheit) zeile.einheit->SetText(pos.einheit);
        if (zeile.preis)   zeile.preis->SetText(pos.einzelpreis.ToString(MoneyStyle::Plain));
        if (zeile.rabatt && pos.rabattPromille != 0)
            zeile.rabatt->SetText(Zahl(pos.rabattPromille / 10));
        if (zeile.konto)   zeile.konto->SetText(pos.konto);
        SteuerlisteFuellen(zeile);
        for (size_t k = 0; k < zeile.vorschlaege.size(); ++k) {
            if (zeile.vorschlaege[k].schluessel.schluessel != pos.steuerschluessel) continue;
            if (zeile.steuer) zeile.steuer->SetSelectedIndex(static_cast<int>(k), false);
            SteuerHinweisAktualisieren(zeile);
            break;
        }
    }
    Neuberechnen();
    return true;
}

} // namespace UltraFIBU
