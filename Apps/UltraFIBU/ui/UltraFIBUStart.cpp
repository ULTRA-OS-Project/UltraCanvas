// Apps/UltraFIBU/ui/UltraFIBUStart.cpp
// The start window. See the header for why it asks so little.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUStart.h"

#include "UltraCanvasNativeDialogs.h"
#include "UltraCanvasModalDialog.h"   // FileFilter

#include "UltraFIBUStore.h"   // DateiExistiert

using namespace UltraCanvas;

namespace UltraFIBU {

namespace {

constexpr int   kBreite = 560;
// Tall enough that the calendar under the date field opens downward and
// leaves the rest of the form visible.
constexpr int   kHoehe  = 640;
constexpr float kRand   = 24.0f;
constexpr float kZeile  = 28.0f;
constexpr float kLabelB = 190.0f;

std::vector<FileFilter> BuchhaltungsFilter() {
    // The type spelled out: FileFilter also takes a single pattern string, and
    // a braced {"db"} matches both constructors.
    return { FileFilter("UltraFIBU-Buchhaltung", std::vector<std::string>{ "db" }),
             FileFilter("Alle Dateien", "*") };
}

} // namespace

std::shared_ptr<UltraCanvasWindow> StartFenster::Bauen(const std::string& hinweis,
                                                       const std::string& vorschlag) {
    vorschlag_ = vorschlag;

    WindowConfig config;
    config.title  = "UltraFIBU";
    config.width  = kBreite;
    config.height = kHoehe;
    fenster_ = CreateWindow(config);

    float y = kRand;
    auto titel = CreateLabel("startTitel", kRand, y, kBreite - 2 * kRand, 30,
                             "UltraFIBU - Buchhaltung");
    titel->SetFontSize(18);
    fenster_->AddChild(titel);
    y += 36;

    // Why this window instead of a bookkeeping. Without it, a user who typed a
    // path would not know whether it was wrong or simply new.
    auto grundText = CreateLabel("startHinweis", kRand, y, kBreite - 2 * kRand, 40, hinweis);
    grundText->SetWrap(TextWrap::WrapWord);
    fenster_->AddChild(grundText);
    y += 50;

    // ---- open an existing one ----
    auto oeffnenKnopf = CreateButton("startOeffnen", kRand, y, 300, kZeile,
                                     "Bestehende Buchhaltung öffnen ...");
    oeffnenKnopf->SetOnClick([this]() { Oeffnen(); });
    fenster_->AddChild(oeffnenKnopf);
    y += kZeile + 26;

    // ---- set up a new one ----
    auto neuTitel = CreateLabel("startNeuTitel", kRand, y, kBreite - 2 * kRand, 24,
                                "Neue Buchhaltung anlegen");
    neuTitel->SetFontSize(14);
    fenster_->AddChild(neuTitel);
    y += 32;

    fenster_->AddChild(CreateLabel("startFirmaL", kRand, y, kLabelB, kZeile, "Firma"));
    firma_ = CreateTextInput("startFirma", static_cast<int>(kRand + kLabelB), static_cast<int>(y),
                             static_cast<int>(kBreite - 2 * kRand - kLabelB),
                             static_cast<int>(kZeile));
    firma_->SetPlaceholder("Name, wie er auf Rechnungen steht");
    firma_->onTextChanged = [this](const std::string&) { Pruefen(); };
    fenster_->AddChild(firma_);
    y += kZeile + 10;

    fenster_->AddChild(CreateLabel("startBeginnL", kRand, y, kLabelB, kZeile,
                                   "Geschäftsjahr beginnt am"));
    beginn_ = CreateDatePicker("startBeginn", kRand + kLabelB, y, 160, kZeile);
    beginn_->SetDateFormat("dd.MM.yyyy");
    beginn_->SetPlaceholder("TT.MM.JJJJ");
    beginn_->SetFirstDayOfWeek(1);   // the German week begins on Monday
    // Only firsts of a month are selectable: that is the rule, shown where the
    // choice is made instead of reported after it.
    beginn_->SetDateEnabledPredicate([](const UCDate& d) { return d.day == 1; });
    // Deliberately empty. A plausible date filled in here is the one a user
    // clicks past - and a wrong start of the fiscal year puts every period in
    // the wrong place, which cannot be put right once documents are booked.
    // The form says the date is missing until one is chosen.
    beginn_->onDateChanged = [this](const UCDate&) { Pruefen(); };
    fenster_->AddChild(beginn_);
    y += kZeile + 10;

    fenster_->AddChild(CreateLabel("startSkrL", kRand, y, kLabelB, kZeile, "Kontenrahmen"));
    skr_ = CreateDropdown("startSkr", kRand + kLabelB, y, 260, kZeile);
    skr_->AddItem("SKR03 - Prozessgliederung", "SKR03");
    skr_->AddItem("SKR04 - Abschlussgliederung", "SKR04");
    skr_->SetSelectedIndex(0, false);
    skr_->onSelectionChanged = [this](int, const DropdownItem&) { Pruefen(); };
    fenster_->AddChild(skr_);
    y += kZeile + 10;

    auto spaeter = CreateLabel("startSpaeter", kRand, y, kBreite - 2 * kRand, 36,
        "Anschrift, Steuernummer und Bankverbindung lassen sich später ergänzen - "
        "sie werden erst für die erste gedruckte Rechnung gebraucht.");
    spaeter->SetWrap(TextWrap::WrapWord);
    spaeter->SetFontSize(11);
    fenster_->AddChild(spaeter);
    y += 44;

    // The reason the button is greyed, next to it - never a greyed button with
    // no explanation.
    grund_ = CreateLabel("startGrund", kRand, y, kBreite - 2 * kRand, 22, "");
    grund_->SetFontSize(11);
    fenster_->AddChild(grund_);
    y += 26;

    anlegenKnopf_ = CreateButton("startAnlegen", kRand, y, 300, kZeile,
                                 "Anlegen und speichern unter ...");
    anlegenKnopf_->SetOnClick([this]() { Anlegen(); });
    fenster_->AddChild(anlegenKnopf_);
    y += kZeile + 12;

    status_ = CreateLabel("startStatus", kRand, y, kBreite - 2 * kRand, 40, "");
    status_->SetWrap(TextWrap::WrapWord);
    fenster_->AddChild(status_);

    Pruefen();
    return fenster_;
}

void StartFenster::Schliessen() {
    if (fenster_) fenster_->PerformClose();
}

EinrichtungsDaten StartFenster::Daten() const {
    EinrichtungsDaten daten;
    if (firma_) daten.firma = firma_->GetText();
    if (beginn_) {
        const UCDate d = beginn_->GetSelectedDate();
        if (d.IsValid()) daten.gjBeginn = Date(d.year, d.month, d.day);
    }
    if (skr_) {
        const DropdownItem* gewaehlt = skr_->GetSelectedItem();
        if (gewaehlt != nullptr) daten.skr = gewaehlt->value;
    }
    return daten;
}

void StartFenster::Pruefen() {
    if (!anlegenKnopf_ || !grund_) return;
    // The same check the setup runs, so the form can never allow what the
    // engine then refuses - and never refuse what it would allow.
    const std::string grund = PruefeEinrichtung(Daten());
    anlegenKnopf_->SetDisabled(!grund.empty());
    grund_->SetText(grund);
}

void StartFenster::Melden(const std::string& text, bool fehler) {
    if (!status_) return;
    status_->SetText(text);
    status_->SetTextColor(fehler ? Color(176, 32, 32) : Color(40, 40, 40));
}

void StartFenster::Anlegen() {
    const EinrichtungsDaten daten = Daten();
    const std::string grund = PruefeEinrichtung(daten);
    if (!grund.empty()) { Melden(grund, true); return; }

    // A path, not bytes: SQLite writes to a file on disk, which is why this
    // uses SaveFile rather than SaveContent. (SaveContent exists for Android,
    // where the chooser yields no filesystem path - UltraFIBU does not run
    // there.)
    const std::string pfad = UltraCanvasNativeDialogs::SaveFile(
        "Neue Buchhaltung speichern unter", BuchhaltungsFilter(), "",
        vorschlag_.empty() ? std::string("buch.db") : vorschlag_, fenster_.get());
    if (pfad.empty()) return;   // cancelled - nothing to say

    Melden("Die Buchhaltung wird angelegt ...", false);
    const EinrichtungsBericht bericht = RichteBuchhaltungEin(pfad, daten);
    if (!bericht.ok) { Melden(bericht.fehler, true); return; }

    Uebergeben(pfad);
}

void StartFenster::Oeffnen() {
    const std::string pfad = UltraCanvasNativeDialogs::OpenFile(
        "Buchhaltung öffnen", BuchhaltungsFilter(), "", fenster_.get());
    if (pfad.empty()) return;

    // Said here rather than by the main window failing to start: an empty file
    // is the leftover of an earlier version opening a mistyped path, and the
    // answer to it is the form directly below.
    if (!EnthaeltBuchhaltung(pfad)) {
        Melden("In \"" + pfad + "\" ist keine Buchhaltung angelegt. Mit dem "
               "Formular oben lässt sie sich dort einrichten.", true);
        vorschlag_ = pfad;
        return;
    }
    Uebergeben(pfad);
}

void StartFenster::Uebergeben(const std::string& pfad) {
    const std::string fehler = onDateiGewaehlt
        ? onDateiGewaehlt(pfad)
        : std::string("Es ist niemand da, der die Buchhaltung öffnet.");
    if (!fehler.empty()) { Melden(fehler, true); return; }
    // Only now: the main window is open, so closing this one does not leave
    // the application without a window.
    Schliessen();
}

} // namespace UltraFIBU
