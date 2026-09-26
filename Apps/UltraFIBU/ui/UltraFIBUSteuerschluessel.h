// Apps/UltraFIBU/ui/UltraFIBUSteuerschluessel.h
// Konfiguration > Steuerschlüssel: the tax keys of this bookkeeping, and a form
// to change them.
//
// Until now a tax key could only be changed by editing Steuerschluessel.csv
// before the bookkeeping was set up. Which day a key starts to apply, which
// rate and which DATEV BU key it carries, which UStVA Kennzahl it reports
// under - those are decisions for the bookkeeping, and they are made here.
//
// **The form does not decide what may change; the store does.** A key that
// postings were made under cannot change what it computes, because the UStVA
// looks each posting's key up again and would report it differently - and a
// period whose return is filed cannot be touched at all. The form shows how
// many postings a version carries, so a refusal is not a surprise, and says
// the store's reason when one comes.
//
// **A rate change is a new version, not an edit.** "Als neue Fassung
// speichern" ends the selected version the day before the date in "gilt ab"
// and starts the form's values from that day. Postings before it keep the
// version they were made under. "Speichern" changes the selected version in
// place, which is for a description, or for a key nothing is posted under
// yet.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers first (X11 defines Bool/Status as macros; the engine
// headers below use those words as identifiers).
#include "UltraCanvasWindow.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasDatePicker.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"

#include "UltraFIBUTabelle.h"

#include "UltraFIBUStore.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraFIBU {

class SteuerschluesselSeite {
public:
    SteuerschluesselSeite(Store& store, const Mandant& mandant, const Akteur& akteur)
        : store_(store), mandant_(mandant), akteur_(akteur) {}

    // Status-line text: what was saved, or why not.
    std::function<void(const std::string&)> onMeldung;

    // The page for a tab. `fenster` is the parent of the confirmation asked
    // before deleting.
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Bauen(
        const std::string& id, float breite, float hoehe,
        UltraCanvas::UltraCanvasWindow* fenster);

    // Re-read the table from the store, keeping the selection if it still exists.
    void Fuellen();

    // An empty form for a key that does not exist yet.
    void Neu();

private:
    void Auswaehlen(int64_t id);
    // The form as a key. Returns false with the reason when a field cannot be
    // read at all (a rate that is not a number) - everything else is the
    // store's check.
    bool AusFormular(Steuerschluessel& out, std::string& fehler) const;
    void Pruefen();
    void Speichern();
    void NeueFassungSpeichern();
    void Loeschen();
    void Melden(const std::string& text);
    void Ablehnen(const std::string& text);   // a refusal: form and status line

    Store&         store_;
    const Mandant& mandant_;
    const Akteur&  akteur_;

    UltraCanvas::UltraCanvasWindow* fenster_ = nullptr;
    std::vector<Steuerschluessel>   geladen_;
    std::vector<int>                buchungen_;   // per entry in geladen_
    Steuerschluessel                gewaehlt_;    // id 0: the form is a new key

    TabellenPanel tabelle_;

    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  name_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  bezeichnung_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown>   art_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  satz_;
    std::shared_ptr<UltraCanvas::UltraCanvasCheckbox>   vorsteuer_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  land_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  bu_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  kzBemessung_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  kzSteuer_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  kontoUmsatz_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  kontoSteuer_;
    std::shared_ptr<UltraCanvas::UltraCanvasDatePicker> von_;
    std::shared_ptr<UltraCanvas::UltraCanvasDatePicker> bis_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>      titel_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>      nutzung_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>      grund_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>     speichern_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>     fassung_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>     loeschen_;

    // Set while the form is being filled from a row, so the change callbacks
    // it triggers do not each run the check.
    bool fuelltFormular_ = false;
};

} // namespace UltraFIBU
