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
#include "UltraCanvasMenu.h"

#include "UltraFIBUTabelle.h"
#include "UltraFIBUBelegDialog.h"

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
    void EuSaetzeFuellen();

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

    // ---- Konfiguration: EU-Steuersätze ------------------------------------
    //
    // The one screen in this window that writes master data, and the reason it
    // is here rather than in the CLI alone: a member state changes its VAT rate
    // with a few weeks' notice, and a bookkeeper who cannot enter that without
    // a new release will enter the wrong rate on every invoice until one
    // arrives.
    //
    // **The dialog can only add.** There is no "edit this rate", because a rate
    // that changed is a new row from the day it changed - overwriting one would
    // silently change what an already-filed return recomputes to. The store
    // enforces that; this asks for the three things a new row needs.
    void EuSatzDialogOeffnen();
    void EuSatzAnlegen(const std::string& land, const std::string& satz,
                       const std::string& abDatum, bool geprueft,
                       const std::string& quelle);
    void GewaehltenEuSatzLoeschen();
    void EuSaetzeUebernehmen();
    void KonfigurationOeffnen();

    // ---- Belege erfassen ---------------------------------------------------
    // The entry form, as its own tab rather than a modal window: entering a
    // document means looking things up - what the customer is called, what was
    // invoiced last time - and a modal that covers the lists makes that a
    // sequence of cancels.
    void BelegFormularOeffnen(BelegArt art);
    void GewaehltenBelegBearbeiten();

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
    std::shared_ptr<UltraCanvas::UltraCanvasMenu>            menue_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>          satzNeuKnopf_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>          satzLoeschenKnopf_;

    TabellenPanel belege_;
    TabellenPanel journal_;
    TabellenPanel salden_;
    TabellenPanel partner_;
    TabellenPanel euSaetze_;

    std::unique_ptr<BelegDialog> formular_;
    int                          formularReiter_ = -1;

    std::vector<EuSteuersatz> geladeneEuSaetze_;
    int64_t                   gewaehlterEuSatz_ = 0;
    // Which tab the EU rate screen sits on, so the Config menu can select it.
    int                       euSaetzeReiter_ = -1;
};

} // namespace UltraFIBU
