// Apps/UltraFIBU/ui/UltraFIBUApp.h
// The UltraFIBU window: a header saying which company and fiscal year is open
// and how far it is frozen, and four screens over the engine - Belege, Journal,
// Summen- und Salden, Partner.
//
// Every screen is a `TabellenPanel`, so sorting, filtering and the
// selection-to-record-id mapping are written once. The screens differ only in
// which columns they declare and which store call fills them, which is what a
// bookkeeping front end mostly is.
//
// **Read-only, deliberately, with two exceptions.** Entering a document needs a
// form with a position editor, and that is its own piece of work; until it
// exists `ultrafibu beleg-neu` writes and these screens show. The two actions
// that *are* here - posting a draft and printing an invoice - are one engine
// call each, they are the two things one does to a document that is already
// entered, and every rule that protects the ledger still lives in the store, so
// the button cannot do anything the CLI could not.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers first (X11 defines Bool/Status as macros; the engine
// headers below use those words as identifiers).
#include "UltraCanvasWindow.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"

#include "UltraFIBUTabelle.h"

#include "UltraFIBUStore.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraFIBU {

class FibuApp {
public:
    // Open `datenbank` and read the company and fiscal year out of it. Returns
    // false with a German sentence in `fehler` when there is nothing to show -
    // a missing file, or a file with no Mandant in it yet.
    bool Initialisieren(const std::string& datenbank, std::string& fehler);

    std::shared_ptr<UltraCanvas::UltraCanvasWindow> FensterBauen();

    // Re-read every screen from the database. Called after anything writes.
    void Aktualisieren();

private:
    void BelegeFuellen();
    void JournalFuellen();
    void SaldenFuellen();
    void PartnerFuellen();

    void KopfAktualisieren();
    void Melden(const std::string& text);

    // The two actions. Both go through the store, so a frozen period, a wrong
    // role or an already-posted document is refused there and reported here.
    void GewaehltenBelegBuchen();
    // Take PDFs in and make a draft of each. Both the button and a drop onto
    // the window end up here, so there is one path and one set of messages.
    void BelegeHochladen(const std::vector<std::string>& pfade);
    void BelegDialogOeffnen();
    void GewaehltenBelegDrucken();

    Store        store_;
    Mandant      mandant_;
    Geschaeftsjahr jahr_;
    Akteur       akteur_;
    bool         jahrGefunden_ = false;

    int64_t      gewaehlterBeleg_ = 0;

    // What each screen last loaded, kept so the summary line can be recomputed
    // over the rows a filter leaves visible rather than over everything.
    std::vector<Beleg>              geladeneBelege_;
    std::vector<Buchung>            geladeneBuchungen_;
    std::vector<Store::KontoSaldo>  geladeneSalden_;
    std::vector<Partner>            geladenePartner_;

    std::shared_ptr<UltraCanvas::UltraCanvasWindow>          fenster_;
    std::shared_ptr<UltraCanvas::UltraCanvasTabbedContainer> reiter_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>           kopf_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>           status_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>          buchenKnopf_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>          druckenKnopf_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>          hochladenKnopf_;

    TabellenPanel belege_;
    TabellenPanel journal_;
    TabellenPanel salden_;
    TabellenPanel partner_;
};

} // namespace UltraFIBU
