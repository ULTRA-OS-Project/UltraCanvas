// Apps/UltraFIBU/ui/UltraFIBUStart.cpp
// The start window. See the header for why it asks so little.
//
// Version: 0.3.1 - SeiteBauen builds the page without a window
// Version: 0.3.0 - on the framework's UltraCanvasStartPage
// Version: 0.2.0 - the bookkeepings opened last, offered first
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUStart.h"

#include "UltraCanvasNativeDialogs.h"
#include "UltraCanvasModalDialog.h"   // FileFilter

#include "UltraFIBUStore.h"   // DateiExistiert
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8

#include <algorithm>

// ULTRAFIBU_VERSION comes from the build alone: CMake reads the first line of
// Docs/UltraFIBU/CHANGELOG.md (cmake/UltraCanvasVersion.cmake) and passes it as a
// compile definition. No fallback, so a build that lost it fails instead of
// showing a wrong number in the window title.
#ifndef ULTRAFIBU_VERSION
#error "ULTRAFIBU_VERSION is not defined: build through CMake, which reads it from Docs/UltraFIBU/CHANGELOG.md"
#endif

using namespace UltraCanvas;

namespace UltraFIBU {

namespace {

constexpr int   kBreite = 560;
// Tall enough that the calendar under the date field opens downward and
// leaves the rest of the form visible.
constexpr int   kHoehe  = 640;
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
                                                       const std::string& vorschlag,
                                                       const std::vector<std::string>& zuletzt) {
    SeiteBauen(hinweis, vorschlag, zuletzt);

    WindowConfig config;
    config.title  = "UltraFIBU " ULTRAFIBU_VERSION;
    config.width  = seite_->Width();
    // At least kHoehe: the calendar under the date field opens downward and
    // needs the room under the form.
    config.height = std::max(seite_->PreferredHeight(), kHoehe);
    fenster_ = CreateWindow(config);
    fenster_->AddChild(seite_);
    return fenster_;
}

std::shared_ptr<UltraCanvasStartPage> StartFenster::SeiteBauen(const std::string& hinweis,
                                                               const std::string& vorschlag,
                                                               const std::vector<std::string>& zuletzt) {
    vorschlag_ = vorschlag;

    // The framework's start page, with UltraFIBU's words: the title, why
    // this window, the files opened last, the open button, then the setup
    // form in a section of its own.
    seite_ = std::make_shared<UltraCanvasStartPage>("start", kBreite);
    seite_->SetTitle("UltraFIBU - Buchhaltung");
    seite_->SetHint(hinweis);
    seite_->SetRecent("Zuletzt geöffnet", zuletzt, [this](const std::string& pfad) {
        if (DateiExistiert(pfad) && EnthaeltBuchhaltung(pfad)) {
            Uebergeben(pfad);
            return;
        }
        // Gone since, or emptied: said here, and dropped from the list.
        Melden("\"" + pfad + "\" gibt es nicht mehr, oder es ist keine Buchhaltung mehr darin.", true);
        if (onDateiVergessen) onDateiVergessen(pfad);
    });
    seite_->AddAction("Bestehende Buchhaltung öffnen ...", [this]() { Oeffnen(); });

    // ---- set up a new one ----
    const int breite = seite_->ContentWidth();
    auto form = seite_->AddSection("Neue Buchhaltung anlegen", 222);
    float y = 0;
    form->AddChild(CreateLabel("startFirmaL", 0, y, kLabelB, kZeile, "Firma"));
    firma_ = CreateTextInput("startFirma", static_cast<int>(kLabelB), static_cast<int>(y),
                             static_cast<int>(breite - kLabelB), static_cast<int>(kZeile));
    firma_->SetPlaceholder("Name, wie er auf Rechnungen steht");
    firma_->onTextChanged = [this](const std::string&) { Pruefen(); };
    form->AddChild(firma_);
    y += kZeile + 10;

    form->AddChild(CreateLabel("startBeginnL", 0, y, kLabelB, kZeile, "Geschäftsjahr beginnt am"));
    beginn_ = CreateDatePicker("startBeginn", kLabelB, y, 160, kZeile);
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
    form->AddChild(beginn_);
    y += kZeile + 10;

    form->AddChild(CreateLabel("startSkrL", 0, y, kLabelB, kZeile, "Kontenrahmen"));
    skr_ = CreateDropdown("startSkr", kLabelB, y, 260, kZeile);
    skr_->AddItem("SKR03 - Prozessgliederung", "SKR03");
    skr_->AddItem("SKR04 - Abschlussgliederung", "SKR04");
    skr_->SetSelectedIndex(0, false);
    skr_->onSelectionChanged = [this](int, const DropdownItem&) { Pruefen(); };
    form->AddChild(skr_);
    y += kZeile + 10;

    auto spaeter = CreateLabel("startSpaeter", 0, y, breite, 36,
        "Anschrift, Steuernummer und Bankverbindung lassen sich später ergänzen - "
        "sie werden erst für die erste gedruckte Rechnung gebraucht.");
    spaeter->SetWrap(TextWrap::WrapWord);
    spaeter->SetFontSize(11);
    form->AddChild(spaeter);
    y += 44;

    // The reason the button is greyed, next to it - never a greyed button with
    // no explanation.
    grund_ = CreateLabel("startGrund", 0, y, breite, 22, "");
    grund_->SetFontSize(11);
    form->AddChild(grund_);
    y += 26;

    anlegenKnopf_ = CreateButton("startAnlegen", 0, y, 300, kZeile,
                                 "Anlegen und speichern unter ...");
    anlegenKnopf_->SetOnClick([this]() { Anlegen(); });
    form->AddChild(anlegenKnopf_);
    seite_->AddStatusLine();

    Pruefen();
    return seite_;
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

std::string StartFenster::Grund() const {
    // The same check the setup runs, so the form can never allow what the
    // engine then refuses - and never refuse what it would allow.
    return PruefeEinrichtung(Daten());
}

void StartFenster::Pruefen() {
    if (!anlegenKnopf_ || !grund_) return;
    const std::string grund = Grund();
    anlegenKnopf_->SetDisabled(!grund.empty());
    grund_->SetText(grund);
}

void StartFenster::Melden(const std::string& text, bool fehler) {
    if (seite_) seite_->Say(text, fehler);
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
