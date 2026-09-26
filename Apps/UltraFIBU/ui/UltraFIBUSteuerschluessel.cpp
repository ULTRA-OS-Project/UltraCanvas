// Apps/UltraFIBU/ui/UltraFIBUSteuerschluessel.cpp
// See the header: which keys there are, and a form whose rules the store keeps.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUSteuerschluessel.h"

#include "UltraCanvasModalDialog.h"

#include <iterator>

namespace UltraFIBU {

using namespace UltraCanvas;

namespace {

constexpr float kFormB   = 390.0f;   // the form beside the table
constexpr float kLabelB  = 120.0f;
constexpr float kZeile   = 26.0f;
constexpr float kAbstand = 29.0f;

// Every kind, in the order a bookkeeper meets them, with the German name the
// form shows. The value is what the store keeps.
struct ArtEintrag { SteuerArt art; const char* text; };
constexpr ArtEintrag kArten[] = {
    { SteuerArt::Inland,             "Inland" },
    { SteuerArt::IgLieferung,        "Innergemeinschaftliche Lieferung" },
    { SteuerArt::IgErwerb,           "Innergemeinschaftlicher Erwerb" },
    { SteuerArt::EuSonstigeLeistung, "Sonstige Leistung EU (§ 3a)" },
    { SteuerArt::Drittland,          "Drittland" },
    { SteuerArt::ReverseCharge13b,   "Reverse Charge (§ 13b)" },
    { SteuerArt::Oss,                "One-Stop-Shop" },
    { SteuerArt::Kleinunternehmer,   "Kleinunternehmer (§ 19)" },
    { SteuerArt::NichtSteuerbar,     "Nicht steuerbar" },
};

UCDate AlsUCDate(const Date& d) {
    return d.Valid() ? UCDate(d.year, d.month, d.day) : UCDate();
}

Date AusUCDate(const UCDate& d) {
    return d.IsValid() ? Date(d.year, d.month, d.day) : Date();
}

std::string Zeitraum(const Steuerschluessel& k) {
    std::string text = k.gueltigVon.Valid() ? FormatDateGerman(k.gueltigVon) : "Beginn";
    return text + " - " + (k.gueltigBis.Valid() ? FormatDateGerman(k.gueltigBis) : "offen");
}

} // namespace

// ===== BUILDING =====

std::shared_ptr<UltraCanvasContainer> SteuerschluesselSeite::Bauen(const std::string& id,
                                                                  float breite, float hoehe,
                                                                  UltraCanvasWindow* fenster) {
    fenster_ = fenster;
    auto seite = CreateContainer(id, 0, 0, breite, hoehe);

    const float tabelleB = breite - kFormB - 12;
    seite->AddChild(tabelle_.Bauen(id + "Tabelle", 0, 0, tabelleB, hoehe,
                                   "Schlüssel, Bezeichnung oder Konto suchen ..."));
    tabelle_.onAuswahlGeaendert = [this](int64_t zeile) { Auswaehlen(zeile); };

    const float x  = tabelleB + 12;
    const float fx = x + kLabelB;
    const float fb = kFormB - kLabelB - 8;
    float y = 4;

    titel_ = CreateLabel(id + "Titel", x, y, kFormB - 8, kZeile, "");
    titel_->SetFontSize(14);
    seite->AddChild(titel_);
    y += kAbstand + 2;

    // One row: a label and whatever field follows it.
    auto zeile = [&](const std::string& name, const std::string& text) {
        seite->AddChild(CreateLabel(id + name + "L", x, y, kLabelB - 6, kZeile, text));
    };
    auto textFeld = [&](const std::string& name, const std::string& text,
                        const std::string& hinweis, float feldB) {
        zeile(name, text);
        auto feld = CreateTextInput(id + name, static_cast<int>(fx), static_cast<int>(y),
                                    static_cast<int>(feldB), static_cast<int>(kZeile));
        feld->SetPlaceholder(hinweis);
        feld->onTextChanged = [this](const std::string&) { Pruefen(); };
        seite->AddChild(feld);
        y += kAbstand;
        return feld;
    };

    name_        = textFeld("Name", "Schlüssel", "z. B. USt19", fb);
    bezeichnung_ = textFeld("Bez", "Bezeichnung", "wie er in Listen erscheint", fb);

    zeile("Art", "Art");
    art_ = CreateDropdown(id + "Art", fx, y, fb, kZeile);
    for (const ArtEintrag& a : kArten) art_->AddItem(a.text, SteuerArtToText(a.art));
    art_->SetSelectedIndex(0, false);
    art_->onSelectionChanged = [this](int, const DropdownItem&) { Pruefen(); };
    seite->AddChild(art_);
    y += kAbstand;

    satz_ = textFeld("Satz", "Steuersatz (%)", "z. B. 19", 100);
    // The checkbox shares the rate's row: whether the tax is owed or
    // deducted is the other half of what the rate means.
    vorsteuer_ = UltraCanvasCheckbox::CreateCheckbox(id + "Vorsteuer", fx + 112, y - kAbstand,
                                                     fb - 112, kZeile, "Vorsteuer");
    vorsteuer_->onStateChanged = [this](CheckedState, CheckedState) { Pruefen(); };
    seite->AddChild(vorsteuer_);

    land_        = textFeld("Land", "Land", "z. B. DE", 100);
    bu_          = textFeld("Bu", "DATEV-BU", "leer: keiner", 100);
    kzBemessung_ = textFeld("KzB", "Kz Bemessung", "z. B. 81", 100);
    kzSteuer_    = textFeld("KzS", "Kz Steuer", "z. B. 66", 100);
    // "Konto" alone: the revenue account on a sales key, the expense account
    // on a purchase key - the placeholder says which, the label cannot fit both.
    kontoUmsatz_ = textFeld("Konto", "Konto", "Erlös/Aufwand, z. B. 8400", fb);
    kontoSteuer_ = textFeld("Steuerkonto", "Steuerkonto", "z. B. 1776", 100);

    auto datum = [&](const std::string& name, const std::string& text) {
        zeile(name, text);
        auto feld = CreateDatePicker(id + name, fx, y, 150, kZeile);
        feld->SetDateFormat("dd.MM.yyyy");
        feld->SetPlaceholder("TT.MM.JJJJ");
        feld->SetFirstDayOfWeek(1);
        feld->onDateChanged = [this](const UCDate&) { Pruefen(); };
        seite->AddChild(feld);
        y += kAbstand;
        return feld;
    };
    von_ = datum("Von", "gilt ab");
    bis_ = datum("Bis", "gilt bis");
    bis_->SetPlaceholder("offen");

    y += 4;
    // What this version carries, and so what may still change about it.
    nutzung_ = CreateLabel(id + "Nutzung", x, y, kFormB - 8, 60, "");
    nutzung_->SetWrap(TextWrap::WrapWord);
    nutzung_->SetFontSize(11);
    nutzung_->SetAlignment(TextAlignment::Left, VerticalAlignment::Top);
    seite->AddChild(nutzung_);
    y += 62;

    // The reason the button is greyed, next to it.
    grund_ = CreateLabel(id + "Grund", x, y, kFormB - 8, 60, "");
    grund_->SetWrap(TextWrap::WrapWord);
    grund_->SetFontSize(11);
    grund_->SetTextColor(Color(176, 32, 32));
    grund_->SetAlignment(TextAlignment::Left, VerticalAlignment::Top);
    seite->AddChild(grund_);
    y += 62;

    const float knopfB = (kFormB - 16) / 2;
    // The second button carries the longer text and gets the room for it.
    speichern_ = CreateButton(id + "Speichern", x, y, 120, kZeile + 4, "Speichern");
    speichern_->SetOnClick([this]() { Speichern(); });
    seite->AddChild(speichern_);
    fassung_ = CreateButton(id + "Fassung", x + 128, y, kFormB - 16 - 120, kZeile + 4,
                            "Als neue Fassung speichern");
    fassung_->SetOnClick([this]() { NeueFassungSpeichern(); });
    seite->AddChild(fassung_);
    y += kZeile + 12;

    auto neu = CreateButton(id + "Neu", x, y, knopfB, kZeile + 4, "Neuer Schlüssel");
    neu->SetOnClick([this]() { Neu(); });
    seite->AddChild(neu);
    loeschen_ = CreateButton(id + "Loeschen", x + knopfB + 8, y, knopfB, kZeile + 4,
                             "Fassung löschen");
    loeschen_->SetOnClick([this]() { Loeschen(); });
    seite->AddChild(loeschen_);

    Neu();
    return seite;
}

// ===== TABLE =====

void SteuerschluesselSeite::Fuellen() {
    tabelle_.SetSpalten({
        ListColumnDef("Schlüssel", 95, TextAlignment::Left),
        ListColumnDef("Bezeichnung", 135, TextAlignment::Left),
        ListColumnDef("Satz", 85, TextAlignment::Right),
        ListColumnDef("BU", 35, TextAlignment::Left),
        ListColumnDef("Kz", 50, TextAlignment::Left),
        ListColumnDef("Konto", 55, TextAlignment::Left),
        ListColumnDef("St.-Kto", 60, TextAlignment::Left),
        ListColumnDef("gilt ab", 95, TextAlignment::Left),
        ListColumnDef("gilt bis", 95, TextAlignment::Left),
        ListColumnDef("Buch.", 50, TextAlignment::Right),
    });

    geladen_ = store_.SteuerschluesselListe(mandant_.id);
    buchungen_.clear();
    std::vector<TabellenZeile> zeilen;
    zeilen.reserve(geladen_.size());
    for (const Steuerschluessel& k : geladen_) {
        // Per version, not per key: whether *this* version carries postings
        // is what decides what may still change about it.
        const int n = store_.SteuerschluesselGebucht(mandant_.id, k.schluessel,
                                                     k.gueltigVon, k.gueltigBis).buchungen;
        buchungen_.push_back(n);
        std::string kz = k.kzBemessung;
        if (!k.kzSteuer.empty()) kz += (kz.empty() ? "" : "/") + k.kzSteuer;
        TabellenZeile zeile;
        zeile.id = k.id;
        zeile.zellen = {
            TabellenZelle(k.schluessel),
            TabellenZelle(k.bezeichnung),
            TabellenZelle(ProzentText(k.satzPromille) + (k.vorsteuer ? " VSt" : ""),
                          k.satzPromille),
            TabellenZelle(k.datevBu),
            TabellenZelle(kz),
            TabellenZelle(k.kontoUmsatz),
            TabellenZelle(k.kontoSteuer),
            DatumsZelle(k.gueltigVon),
            k.gueltigBis.Valid() ? DatumsZelle(k.gueltigBis) : TabellenZelle(std::string("offen")),
            TabellenZelle(Zahl(n), n),
        };
        zeilen.push_back(std::move(zeile));
    }
    tabelle_.SetZeilen(std::move(zeilen));
    tabelle_.SetFussFunktion([this](const std::vector<int64_t>& ids) {
        int mitBuchungen = 0;
        for (const int64_t id : ids)
            for (size_t i = 0; i < geladen_.size(); ++i)
                if (geladen_[i].id == id && buchungen_[i] > 0) { ++mitBuchungen; break; }
        std::string text = Zahl(static_cast<int64_t>(ids.size())) + " Fassung(en)";
        if (ids.size() != geladen_.size())
            text += " von " + Zahl(static_cast<int64_t>(geladen_.size())) + " (gefiltert)";
        return text + ", davon " + Zahl(mitBuchungen) + " mit Buchungen";
    });

    // The version on the form may have been ended or deleted meanwhile.
    if (gewaehlt_.id != 0) {
        bool gibtEs = false;
        for (const Steuerschluessel& k : geladen_) gibtEs = gibtEs || k.id == gewaehlt_.id;
        if (gibtEs) {
            Auswaehlen(gewaehlt_.id);
            tabelle_.Auswaehlen(gewaehlt_.id);   // the row the form now shows
        } else {
            Neu();
        }
    }
}

// ===== FORM =====

void SteuerschluesselSeite::Neu() {
    gewaehlt_ = Steuerschluessel();
    fuelltFormular_ = true;
    for (auto* feld : { &name_, &bezeichnung_, &satz_, &land_, &bu_, &kzBemessung_,
                        &kzSteuer_, &kontoUmsatz_, &kontoSteuer_ })
        if (*feld) (*feld)->SetText("");
    if (name_) name_->SetReadOnly(false);
    if (art_) art_->SetSelectedIndex(0, false);
    if (vorsteuer_) vorsteuer_->SetChecked(false);
    if (von_) von_->Clear();
    if (bis_) bis_->Clear();
    fuelltFormular_ = false;
    if (titel_) titel_->SetText("Neuer Steuerschlüssel");
    if (nutzung_)
        nutzung_->SetText("Ein neuer Schlüssel, oder eine weitere Fassung eines "
                          "bestehenden für einen Zeitraum, den keine andere abdeckt.");
    Pruefen();
}

void SteuerschluesselSeite::Auswaehlen(int64_t id) {
    const Steuerschluessel* k = nullptr;
    int n = 0;
    for (size_t i = 0; i < geladen_.size(); ++i)
        if (geladen_[i].id == id) { k = &geladen_[i]; n = buchungen_[i]; break; }
    if (k == nullptr) return;
    gewaehlt_ = *k;

    fuelltFormular_ = true;
    name_->SetText(k->schluessel);
    // The name is what postings refer to; a different name is a different key.
    name_->SetReadOnly(true);
    bezeichnung_->SetText(k->bezeichnung);
    for (size_t i = 0; i < std::size(kArten); ++i)
        if (kArten[i].art == k->art) art_->SetSelectedIndex(static_cast<int>(i), false);
    const std::string prozent = ProzentText(k->satzPromille);
    satz_->SetText(prozent.substr(0, prozent.size() - 2));   // the field takes the number alone
    vorsteuer_->SetChecked(k->vorsteuer);
    land_->SetText(k->land);
    bu_->SetText(k->datevBu);
    kzBemessung_->SetText(k->kzBemessung);
    kzSteuer_->SetText(k->kzSteuer);
    kontoUmsatz_->SetText(k->kontoUmsatz);
    kontoSteuer_->SetText(k->kontoSteuer);
    if (k->gueltigVon.Valid()) von_->SetSelectedDate(AlsUCDate(k->gueltigVon)); else von_->Clear();
    if (k->gueltigBis.Valid()) bis_->SetSelectedDate(AlsUCDate(k->gueltigBis)); else bis_->Clear();
    fuelltFormular_ = false;

    titel_->SetText(k->schluessel + ", " + Zeitraum(*k));
    if (n == 0) {
        nutzung_->SetText("In dieser Fassung ist nichts gebucht - sie lässt sich "
                          "frei ändern, solange ihr Zeitraum in keiner abgegebenen "
                          "Meldung liegt.");
    } else {
        const Store::SteuerschluesselNutzung u = store_.SteuerschluesselGebucht(
            mandant_.id, k->schluessel, k->gueltigVon, k->gueltigBis);
        nutzung_->SetText(Zahl(n) + " Buchung(en) in dieser Fassung, die letzte am " +
                          FormatDateGerman(u.letzte) + ". Satz, Art, Kennzahlen und "
                          "Konten ändern sich nur noch als neue Fassung ab einem "
                          "späteren Tag.");
    }
    Pruefen();
}

bool SteuerschluesselSeite::AusFormular(Steuerschluessel& k, std::string& fehler) const {
    k = gewaehlt_;
    k.mandantId   = mandant_.id;
    k.schluessel  = name_->GetText();
    k.bezeichnung = bezeichnung_->GetText();
    if (const DropdownItem* gewaehlt = art_->GetSelectedItem())
        SteuerArtFromText(gewaehlt->value, k.art);
    const std::string satz = satz_->GetText();
    if (satz.empty()) {
        k.satzPromille = 0;
    } else if (!ProzentNachPromille(satz, k.satzPromille)) {
        fehler = "\"" + satz + "\" ist kein Steuersatz zwischen 0 und 100 "
                 "(höchstens eine Nachkommastelle).";
        return false;
    }
    k.vorsteuer   = vorsteuer_->IsChecked();
    k.land        = land_->GetText();
    for (char& c : k.land) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    k.datevBu     = bu_->GetText();
    k.kzBemessung = kzBemessung_->GetText();
    k.kzSteuer    = kzSteuer_->GetText();
    k.kontoUmsatz = kontoUmsatz_->GetText();
    k.kontoSteuer = kontoSteuer_->GetText();
    k.gueltigVon  = AusUCDate(von_->GetSelectedDate());
    k.gueltigBis  = AusUCDate(bis_->GetSelectedDate());
    return true;
}

void SteuerschluesselSeite::Pruefen() {
    if (fuelltFormular_ || !speichern_ || !grund_) return;
    Steuerschluessel k;
    std::string grund;
    // The same check the store runs before it writes, so the button is never
    // enabled for what will be refused as data. Whether postings or a filed
    // return stand in the way is the store's to say on saving - it depends on
    // which of the two buttons is pressed.
    if (AusFormular(k, grund))
        grund = store_.SteuerschluesselPruefen(k, gewaehlt_.id != 0 ? &gewaehlt_ : nullptr);
    grund_->SetText(grund);
    speichern_->SetDisabled(!grund.empty());
    fassung_->SetDisabled(gewaehlt_.id == 0);
    loeschen_->SetDisabled(gewaehlt_.id == 0);
}

void SteuerschluesselSeite::Melden(const std::string& text) {
    if (onMeldung) onMeldung(text);
}

void SteuerschluesselSeite::Ablehnen(const std::string& text) {
    // In the form as well as the status line: the store's reasons run to a
    // few lines, and the status line cuts them off after one.
    if (grund_) grund_->SetText(text);
    Melden(text);
}

// ===== ACTIONS =====

void SteuerschluesselSeite::Speichern() {
    Steuerschluessel k;
    std::string fehler;
    if (!AusFormular(k, fehler)) { Melden(fehler); return; }

    const bool neu = gewaehlt_.id == 0;
    const StoreResult r = neu ? store_.SteuerschluesselAnlegen(k, akteur_)
                              : store_.SteuerschluesselAendern(k, akteur_);
    if (!r) { Ablehnen(r.fehler); return; }
    gewaehlt_.id = k.id;
    Fuellen();
    Melden(k.schluessel + (neu ? " angelegt" : " geändert") + ", gültig " + Zeitraum(k) + ".");
}

void SteuerschluesselSeite::NeueFassungSpeichern() {
    if (gewaehlt_.id == 0) { Melden("Zuerst die Fassung auswählen, die abgelöst wird."); return; }
    Steuerschluessel k;
    std::string fehler;
    if (!AusFormular(k, fehler)) { Ablehnen(fehler); return; }
    const Date ab = k.gueltigVon;
    if (!ab.Valid() || ab == gewaehlt_.gueltigVon) {
        Ablehnen("Unter \"gilt ab\" den Tag eintragen, ab dem die neue Fassung gilt - "
               "er liegt innerhalb der bisherigen (" + Zeitraum(gewaehlt_) + ").");
        return;
    }

    const Steuerschluessel alt = gewaehlt_;
    const StoreResult r = store_.SteuerschluesselNeueFassung(alt.id, ab, k, akteur_);
    if (!r) { Ablehnen(r.fehler); return; }
    gewaehlt_.id = k.id;
    Fuellen();
    Melden(k.schluessel + ": neue Fassung ab " + FormatDateGerman(ab) + " (" +
           ProzentText(k.satzPromille) + "). Die bisherige gilt bis " +
           FormatDateGerman(ab.AddDays(-1)) + " weiter; was davor gebucht ist, bleibt, "
           "wie es ist.");
}

void SteuerschluesselSeite::Loeschen() {
    if (gewaehlt_.id == 0) { Melden("Keine Fassung ausgewählt."); return; }
    const Steuerschluessel k = gewaehlt_;
    // Asked rather than done: a deleted key is gone from every dropdown, and
    // documents still in draft that use it can no longer be posted.
    const std::string frage =
        "Fassung " + k.schluessel + " (" + Zeitraum(k) + ") löschen?\n\n"
        "Löschen ist für einen Schlüssel, der so nie gegolten hat. Ein geänderter "
        "Satz wird nicht gelöscht, sondern als neue Fassung ab seinem Datum erfasst.";
    UltraCanvasDialogManager::ShowConfirmation(
        frage, "Steuerschlüssel löschen",
        [this, k](bool bestaetigt) {
            if (!bestaetigt) return;
            const StoreResult r = store_.SteuerschluesselLoeschen(k.id, akteur_);
            if (!r) { Ablehnen(r.fehler); return; }
            Neu();
            Fuellen();
            Melden(k.schluessel + " (" + Zeitraum(k) + ") gelöscht.");
        },
        fenster_);
}

} // namespace UltraFIBU
